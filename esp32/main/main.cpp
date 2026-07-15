#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "esp_log.h"

#include <cstring>

#include "config.hpp"
#include "kiss_codec.hpp"
#include "radio.hpp"
#include "uart_bridge.hpp"

static const char* TAG = "main";

// ── shared objects ─────────────────────────────────────────────
static Radio      s_radio;
static UartBridge s_uart;
static QueueHandle_t s_rx_queue;   // ESP-NOW → UART path

// ── task: UART RX → ESP-NOW TX ────────────────────────────────
static void uart_rx_task(void*)
{
    KissCodec codec;

    ESP_LOGI(TAG, "uart_rx_task started");

    for (;;) {
        codec.decode_begin();

        for (;;) {
            int byte = s_uart.read_byte();
            if (byte < 0) continue;                 // timeout, keep reading

            if (codec.decode_feed(static_cast<uint8_t>(byte))) {
                break;                              // frame complete
            }
        }

        const KissFrame& f = codec.frame();

        if (f.command == cfg::KISS_CMD_RETURN) {
            ESP_LOGI(TAG, "KISS RETURN received");
            continue;
        }

        if (f.command != cfg::KISS_CMD_DATA) {
            ESP_LOGD(TAG, "ignoring KISS cmd 0x%02x", f.command);
            continue;
        }

        if (f.payload_len == 0) continue;

        ESP_LOGD(TAG, "TX %zu bytes via ESP-NOW", f.payload_len);
        s_radio.send_broadcast(f.payload, f.payload_len);
    }
}

// ── ESP-NOW receive callback → queue ───────────────────────────
struct RxPacket {
    uint8_t  src_mac[6];
    int8_t   rssi;
    uint8_t  len;
    uint8_t  data[cfg::KISS_MAX_FRAME];
};

static void on_radio_recv(const uint8_t* src_mac, int8_t rssi,
                           const uint8_t* data, size_t len)
{
    if (len > cfg::KISS_MAX_FRAME) return;

    RxPacket pkt;
    memcpy(pkt.src_mac, src_mac, 6);
    pkt.rssi = rssi;
    pkt.len  = static_cast<uint8_t>(len);
    memcpy(pkt.data, data, len);

    if (xQueueSend(s_rx_queue, &pkt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "RX queue full, dropping packet");
    }
}

// ── task: ESP-NOW RX → UART TX ────────────────────────────────
static void espnow_rx_task(void*)
{
    uint8_t kiss_buf[cfg::KISS_MAX_FRAME + 16];   // encoded frame
    RxPacket pkt;

    ESP_LOGI(TAG, "espnow_rx_task started");

    for (;;) {
        if (xQueueReceive(s_rx_queue, &pkt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        size_t encoded = KissCodec::encode(pkt.data, pkt.len,
                                           kiss_buf, sizeof(kiss_buf));
        if (encoded > 0) {
            ESP_LOGD(TAG, "RX %u bytes from %02x:%02x:%02x:%02x:%02x:%02x (rssi=%d)",
                     pkt.len,
                     pkt.src_mac[0], pkt.src_mac[1], pkt.src_mac[2],
                     pkt.src_mac[3], pkt.src_mac[4], pkt.src_mac[5],
                     pkt.rssi);

            s_uart.write(kiss_buf, encoded);
        }
    }
}

// ── entry point ────────────────────────────────────────────────
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "KISS modem for RNS — ESP32 build");

    s_rx_queue = xQueueCreate(cfg::QUEUE_SIZE, sizeof(RxPacket));
    assert(s_rx_queue);

    s_uart.init();
    s_radio.init();
    s_radio.set_recv_callback(on_radio_recv);

    xTaskCreate(uart_rx_task,   "uart_rx",   cfg::TASK_STACK, nullptr, cfg::TASK_PRIO, nullptr);
    xTaskCreate(espnow_rx_task, "espnow_rx", cfg::TASK_STACK, nullptr, cfg::TASK_PRIO, nullptr);

    ESP_LOGI(TAG, "modem running");
}
