#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "config.hpp"
#include "kiss_codec.hpp"
#include "radio.hpp"
#include "uart_bridge.hpp"

static const char* TAG = "main";

#if DEBUG_LOGS
#define LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#define LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#define LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#define LOGI_R(rtag, ...) ESP_LOGI(rtag, __VA_ARGS__)
#else
#define LOGI(...) do {} while(0)
#define LOGW(...) do {} while(0)
#define LOGE(...) do {} while(0)
#define LOGI_R(rtag, ...) do {} while(0)
#endif

// ── debug UART output ─────────────────────────────────────────
#if DEBUG_LOGS
static int debug_vprintf(const char* fmt, va_list args)
{
    char buf[512];
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    if (len > 0) {
        uart_write_bytes(cfg::DEBUG_UART_PORT, buf, len);
    }
    return len;
}

static void debug_uart_init()
{
    const uart_config_t uart_cfg = {
        .baud_rate  = cfg::DEBUG_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {},
    };
    uart_driver_install(cfg::DEBUG_UART_PORT, 1024, 0, 0, nullptr, 0);
    uart_param_config(cfg::DEBUG_UART_PORT, &uart_cfg);
    uart_set_pin(cfg::DEBUG_UART_PORT,
                 cfg::DEBUG_TX_PIN, cfg::DEBUG_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    esp_log_set_vprintf(debug_vprintf);
}
#else
static void debug_uart_init() {}
#endif

static void log_hex(const char* tag, const char* prefix,
                     const uint8_t* data, size_t len)
{
#if DEBUG_LOGS
    char hex[1024];
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 3 < sizeof(hex); ++i) {
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02x", data[i]);
    }
    ESP_LOGI(tag, "%s [%zu]: %s", prefix, len, hex);
#else
    (void)tag; (void)prefix; (void)data; (void)len;
#endif
}

// ── raw capture buffer for tests ───────────────────────────────
static uint8_t  s_capture_buf[4096];
static size_t   s_capture_len = 0;

static void capture_raw(uint8_t byte)
{
#if DEBUG_LOGS
    if (s_capture_len < sizeof(s_capture_buf)) {
        s_capture_buf[s_capture_len++] = byte;
    }
#else
    (void)byte;
#endif
}

static void dump_capture()
{
#if DEBUG_LOGS
    if (s_capture_len == 0) return;
    LOGI("=== RAW UART %zu bytes ===", s_capture_len);
    log_hex(TAG, "raw", s_capture_buf, s_capture_len);
    LOGI("=== END RAW ===");
    s_capture_len = 0;
#endif
}

// ── shared objects ─────────────────────────────────────────────
static Radio      s_radio;
static UartBridge s_uart;
static QueueHandle_t s_rx_queue;

static inline void led_on()  {
#if DEBUG_LED
    gpio_set_level(static_cast<gpio_num_t>(cfg::LED_GPIO), 1);
#endif
}
static inline void led_off() {
#if DEBUG_LED
    gpio_set_level(static_cast<gpio_num_t>(cfg::LED_GPIO), 0);
#endif
}

static void led_blink(int count, int ms_on, int ms_off)
{
#if DEBUG_LED
    for (int i = 0; i < count; ++i) {
        led_on();
        vTaskDelay(pdMS_TO_TICKS(ms_on));
        led_off();
        vTaskDelay(pdMS_TO_TICKS(ms_off));
    }
#else
    (void)count; (void)ms_on; (void)ms_off;
#endif
}

// ── fragmentation ──────────────────────────────────────────────
static constexpr uint8_t FRAG_MORE = 0x80;

static void send_fragmented(const uint8_t* data, size_t len)
{
    size_t offset = 0;

    while (offset < len) {
        size_t chunk = len - offset;
        if (chunk > cfg::FRAG_MAX_DATA) {
            chunk = cfg::FRAG_MAX_DATA;
        }

        bool last = (offset + chunk >= len);
        uint8_t header = last ? 0x00 : FRAG_MORE;

        uint8_t pkt[cfg::ESPNOW_MAX_PAYLOAD];
        pkt[0] = header;
        memcpy(pkt + 1, data + offset, chunk);

        LOGI("  frag hdr=0x%02x offset=%zu/%zu chunk=%zu",
                 header, offset, len, chunk);

        bool ok = s_radio.send_broadcast(pkt, 1 + chunk);
        if (!ok) {
            LOGE("  frag send FAILED");
            return;
        }

        offset += chunk;
    }
}

// ── task: UART RX → ESP-NOW TX ────────────────────────────────
static void uart_rx_task(void*)
{
    KissCodec codec;

    LOGI("uart_rx_task started, listening on UART%d", cfg::UART_PORT);

    for (;;) {
        codec.decode_begin();
        s_capture_len = 0;

        for (;;) {
            int byte = s_uart.read_byte();
            if (byte < 0) continue;

            capture_raw(static_cast<uint8_t>(byte));

            if (codec.decode_feed(static_cast<uint8_t>(byte))) {
                break;
            }
        }

        dump_capture();

        const KissFrame& f = codec.frame();

        LOGI("KISS frame: cmd=0x%02x payload_len=%zu",
                 f.command, f.payload_len);

        if (f.payload_len > 0) {
            log_hex(TAG, "KISS payload", f.payload, f.payload_len);
        }

        if (f.command != cfg::KISS_CMD_DATA) {
            LOGI("KISS cmd 0x%02x — config, skipping", f.command);
            led_blink(2, 30, 30);
            continue;
        }
        if (f.payload_len == 0) {
            LOGW("empty DATA frame, skipping");
            continue;
        }

        LOGI("TX %zu bytes via ESP-NOW (%s)",
                 f.payload_len,
                 (f.payload_len > cfg::FRAG_MAX_DATA) ? "fragmented" : "single");

        send_fragmented(f.payload, f.payload_len);

        led_blink(3, 50, 50);
    }
}

// ── ESP-NOW receive callback → queue ───────────────────────────
struct RxPacket {
    uint8_t  src_mac[6];
    int8_t   rssi;
    uint16_t len;
    uint8_t  data[cfg::KISS_MAX_FRAME];
};

static void on_radio_recv(const uint8_t* src_mac, int8_t rssi,
                           const uint8_t* data, size_t len)
{
    if (len < 1 || len > cfg::ESPNOW_MAX_PAYLOAD) {
        LOGW("ESP-NOW RX: bad len %zu, dropping", len);
        return;
    }

    uint8_t header = data[0];
    bool more = (header & FRAG_MORE) != 0;
    const uint8_t* payload = data + 1;
    size_t payload_len = len - 1;

    LOGI("ESP-NOW RX cb: %02x:%02x:%02x:%02x:%02x:%02x len=%zu hdr=0x%02x RSSI=%d",
             src_mac[0], src_mac[1], src_mac[2],
             src_mac[3], src_mac[4], src_mac[5],
             len, header, rssi);

    RxPacket pkt;
    memcpy(pkt.src_mac, src_mac, 6);
    pkt.rssi = rssi;

    static uint8_t reasm_buf[cfg::KISS_MAX_FRAME];
    static size_t  reasm_len = 0;

    if (!more) {
        // last fragment or single — combine with reassembly buffer
        size_t total = reasm_len + payload_len;
        if (total > cfg::KISS_MAX_FRAME) {
            LOGW("reassembly overflow (%zu), dropping", total);
            reasm_len = 0;
            return;
        }
        memcpy(reasm_buf + reasm_len, payload, payload_len);
        pkt.len = static_cast<uint16_t>(total);
        memcpy(pkt.data, reasm_buf, total);
        reasm_len = 0;

        if (xQueueSend(s_rx_queue, &pkt, 0) != pdTRUE) {
            LOGW("RX queue FULL, dropping");
        }
    } else {
        // middle fragment — append to reassembly buffer
        if (reasm_len + payload_len <= cfg::KISS_MAX_FRAME) {
            memcpy(reasm_buf + reasm_len, payload, payload_len);
            reasm_len += payload_len;
            LOGI("  frag stored: %zu (total %zu)", payload_len, reasm_len);
        } else {
            LOGW("  reassembly overflow, dropping");
            reasm_len = 0;
        }
    }
}

// ── task: ESP-NOW RX → UART TX ────────────────────────────────
static void espnow_rx_task(void*)
{
    uint8_t kiss_buf[cfg::KISS_MAX_FRAME * 2 + 3];
    RxPacket pkt;

    LOGI("espnow_rx_task started, sending to UART%d", cfg::UART_PORT);

    for (;;) {
        if (xQueueReceive(s_rx_queue, &pkt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        LOGI("ESP-NOW RX: from %02x:%02x:%02x:%02x:%02x:%02x RSSI=%d len=%u",
                 pkt.src_mac[0], pkt.src_mac[1], pkt.src_mac[2],
                 pkt.src_mac[3], pkt.src_mac[4], pkt.src_mac[5],
                 pkt.rssi, pkt.len);

        log_hex(TAG, "ESP-NOW payload", pkt.data, pkt.len);

        size_t encoded = KissCodec::encode(pkt.data, pkt.len,
                                           kiss_buf, sizeof(kiss_buf));
        if (encoded > 0) {
            log_hex(TAG, "KISS encoded", kiss_buf, encoded);

            bool ok = s_uart.write(kiss_buf, encoded);
            LOGI("UART write %zu bytes: %s", encoded, ok ? "OK" : "FAIL");

            led_blink(1, 100, 0);
        }
    }
}

// ── entry point ────────────────────────────────────────────────
extern "C" void app_main(void)
{
    debug_uart_init();

#if DEBUG_LED
    gpio_reset_pin(static_cast<gpio_num_t>(cfg::LED_GPIO));
    gpio_set_direction(static_cast<gpio_num_t>(cfg::LED_GPIO), GPIO_MODE_OUTPUT);
    led_blink(3, 150, 150);
#endif

#if DEBUG_LOGS
    LOGI("=== KISS modem for RNS — ESP32 build ===");
    esp_log_level_set(TAG, ESP_LOG_VERBOSE);
    esp_log_level_set("radio", ESP_LOG_VERBOSE);
    LOGI("KISS UART%d: GPIO%d TX / GPIO%d RX @ %d baud",
         cfg::UART_PORT, cfg::UART_TX_PIN, cfg::UART_RX_PIN, cfg::UART_BAUD);
    LOGI("Debug UART%d: GPIO%d TX / GPIO%d RX @ %d baud",
         cfg::DEBUG_UART_PORT, cfg::DEBUG_TX_PIN, cfg::DEBUG_RX_PIN, cfg::DEBUG_UART_BAUD);
    LOGI("ESP-NOW channel %d, broadcast, frag_max=%zu",
         cfg::WIFI_CHANNEL, cfg::FRAG_MAX_DATA);
#endif

    s_rx_queue = xQueueCreate(cfg::QUEUE_SIZE, sizeof(RxPacket));
    assert(s_rx_queue);

    s_uart.init();
    s_radio.init();
    s_radio.set_recv_callback(on_radio_recv);

    xTaskCreate(uart_rx_task,   "uart_rx",   cfg::TASK_STACK, nullptr, cfg::TASK_PRIO, nullptr);
    xTaskCreate(espnow_rx_task, "espnow_rx", cfg::TASK_STACK, nullptr, cfg::TASK_PRIO, nullptr);

    LOGI("modem running");
}
