#include <cstring>

#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "app_config.h"
#include "debug.h"
#include "kiss_codec.hpp"
#include "radio.hpp"
#include "uart_bridge.hpp"

#define FRAG_MORE 0x80

static QueueHandle_t s_rx_queue;

static constexpr uint16_t crc16_table[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CC0, 0x0CE1,
    0xEFEF, 0xFFCE, 0xCFAD, 0xDF8C, 0xAF6B, 0xBF4A, 0x8F29, 0x9F08,
    0x6EED, 0x7ECF, 0x4EAF, 0x5E8E, 0x2E69, 0x3E48, 0x0E2B, 0x1E0A
};

static uint16_t crc16_update(uint16_t crc, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        crc = (crc << 8) ^ crc16_table[((crc >> 8) ^ data[i]) & 0xFF];
    }
    return crc;
}

struct RxPacket {
    uint8_t  src_mac[6];
    uint16_t len;
    uint8_t  data[KISS_MAX_FRAME + 2];
};

static void on_radio_recv(const uint8_t *src_mac, const uint8_t *data, size_t len)
{
    debug_printf("[MAIN] on_radio_recv: len=%d\n", static_cast<int>(len));

    if (len < 1 || len > ESPNOW_MAX_PAYLOAD) {
        debug_printf("[MAIN] bad len, drop\n");
        return;
    }

    uint8_t header = data[0];
    if (header & 0x7E) {
        debug_printf("[MAIN] bad hdr 0x%02x, drop\n", header);
        return;
    }
    bool more = (header & FRAG_MORE) != 0;
    const uint8_t *payload = data + 1;
    size_t payload_len = len - 1;

    static uint8_t reasm_buf[KISS_MAX_FRAME + 2];
    static size_t  reasm_len = 0;

    if (more) {
        if (reasm_len + payload_len <= sizeof(reasm_buf)) {
            std::memcpy(reasm_buf + reasm_len, payload, payload_len);
            reasm_len += payload_len;
        } else {
            reasm_len = 0;
        }
        return;
    }

    size_t total = reasm_len + payload_len;
    if (total < 3 || total > sizeof(reasm_buf)) {
        reasm_len = 0;
        return;
    }

    uint16_t calc_crc = 0xFFFF;
    calc_crc = crc16_update(calc_crc, reasm_buf, reasm_len);
    calc_crc = crc16_update(calc_crc, payload, payload_len - 2);

    uint16_t recv_crc = static_cast<uint16_t>((payload[payload_len - 2] << 8) |
                                               payload[payload_len - 1]);

    if (recv_crc != calc_crc) {
        debug_printf("[MAIN] CRC mismatch: recv=0x%04x calc=0x%04x\n", recv_crc, calc_crc);
        reasm_len = 0;
        return;
    }

    debug_printf("[MAIN] frame OK: %d bytes\n", static_cast<int>(total - 2));

    RxPacket pkt;
    std::memcpy(pkt.src_mac, src_mac, 6);
    pkt.len = static_cast<uint16_t>(total - 2);
    std::memcpy(pkt.data, reasm_buf, reasm_len);
    std::memcpy(pkt.data + reasm_len, payload, payload_len - 2);
    reasm_len = 0;

    xQueueSend(s_rx_queue, &pkt, 0);
}

static void send_fragmented(const uint8_t *data, size_t len)
{
    size_t offset = 0;

    while (offset < len) {
        size_t chunk = len - offset;
        if (chunk > FRAG_MAX_DATA) {
            chunk = FRAG_MAX_DATA;
        }

        bool last = (offset + chunk >= len);
        uint8_t header = last ? 0x00 : FRAG_MORE;

        uint8_t pkt[ESPNOW_MAX_PAYLOAD];
        pkt[0] = header;
        std::memcpy(pkt + 1, data + offset, chunk);

        if (radio_send_broadcast(pkt, 1 + chunk) != 0) {
            return;
        }

        offset += chunk;
    }
}

static void uart_rx_task(void *arg)
{
    (void)arg;
    KissCodec codec;

    for (;;) {
        codec.decode_begin();

        for (;;) {
            int byte = uart_bridge_read_byte();
            if (byte < 0) continue;
            if (codec.decode_feed(static_cast<uint8_t>(byte))) {
                break;
            }
        }

        const KissFrame &f = codec.frame();

        if (f.command != KISS_CMD_DATA) {
            debug_printf("[MAIN] KISS cmd 0x%02x, skip\n", f.command);
            continue;
        }
        if (f.payload_len == 0) continue;

        debug_printf("[MAIN] KISS rx %d bytes\n", static_cast<int>(f.payload_len));

        uint16_t crc = crc16_update(0xFFFF, f.payload, f.payload_len);
        uint8_t frame_with_crc[KISS_MAX_FRAME + 2];
        std::memcpy(frame_with_crc, f.payload, f.payload_len);
        frame_with_crc[f.payload_len]     = crc >> 8;
        frame_with_crc[f.payload_len + 1] = crc & 0xFF;
        size_t frame_len = f.payload_len + 2;

        send_fragmented(frame_with_crc, frame_len);
    }
}

static void espnow_rx_task(void *arg)
{
    (void)arg;
    uint8_t kiss_buf[KISS_MAX_FRAME * 2 + 3];
    RxPacket pkt;

    for (;;) {
        if (xQueueReceive(s_rx_queue, &pkt, pdMS_TO_TICKS(5000)) != pdTRUE) {
            debug_printf("[MAIN] espnow_rx: idle 5s, no packets\n");
            continue;
        }

        debug_printf("[MAIN] espnow_rx: got pkt %d bytes\n", pkt.len);
        size_t encoded = KissCodec::encode(pkt.data, pkt.len,
                                           kiss_buf, sizeof(kiss_buf));
        if (encoded > 0) {
            uart_bridge_write(kiss_buf, encoded);
            debug_printf("[MAIN] sent %d bytes to UART\n", static_cast<int>(encoded));
        }
    }
}

extern "C" void app_main(void)
{
    debug_init();
    debug_printf("\n=== ESP8266 KISS modem starting ===\n");

    s_rx_queue = xQueueCreate(QUEUE_SIZE, sizeof(RxPacket));

    debug_printf("[MAIN] init uart...\n");
    uart_bridge_init();

    debug_printf("[MAIN] init radio...\n");
    if (radio_init() != 0) {
        debug_printf("[MAIN] RADIO INIT FAILED!\n");
    } else {
        debug_printf("[MAIN] radio OK\n");
    }

    radio_set_recv_callback(on_radio_recv);
    debug_printf("[MAIN] recv callback registered\n");

    xTaskCreate(uart_rx_task,   "uart_rx",   TASK_STACK, nullptr, TASK_PRIO, nullptr);
    xTaskCreate(espnow_rx_task, "espnow_rx", TASK_STACK, nullptr, TASK_PRIO, nullptr);

    debug_printf("[MAIN] modem running\n");
}
