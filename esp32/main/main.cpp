#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

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
#define LOGI(...) ESP_LOGI(__FILE_NAME__, __VA_ARGS__)
#define LOGW(...) ESP_LOGW(__FILE_NAME__, __VA_ARGS__)
#define LOGE(...) ESP_LOGE(__FILE_NAME__, __VA_ARGS__)
#define LOGI_R(rtag, ...) ESP_LOGI(rtag, __VA_ARGS__)
#else
#define LOGI(...) do {} while(0)
#define LOGW(...) do {} while(0)
#define LOGE(...) do {} while(0)
#define LOGI_R(rtag, ...) do {} while(0)
#endif

#if DEBUG_VERBOSE
#define LOGD(...) ESP_LOGD(__FILE_NAME__, __VA_ARGS__)
#else
#define LOGD(...) do {} while(0)
#endif

// ── async log: ring buffer + low-priority task ─────────────────
#if DEBUG_LOGS
static constexpr size_t LOG_BUF_SIZE    = 8192;
static constexpr size_t LOG_MSG_MAX     = 1200;
static char             s_log_buf[LOG_BUF_SIZE];
static size_t           s_log_head = 0;   // write position
static size_t           s_log_tail = 0;   // read position
static size_t           s_log_used = 0;   // bytes in buffer
static SemaphoreHandle_t s_log_sem;

static size_t log_buf_free() { return LOG_BUF_SIZE - s_log_used; }

static void log_write(const char* data, size_t len)
{
    if (len > log_buf_free()) return;  // drop if full
    for (size_t i = 0; i < len; ++i) {
        s_log_buf[s_log_head] = data[i];
        s_log_head = (s_log_head + 1) % LOG_BUF_SIZE;
    }
    s_log_used += len;
    xSemaphoreGive(s_log_sem);
}

static void log_task(void*)
{
    char chunk[128];
    for (;;) {
        xSemaphoreTake(s_log_sem, pdMS_TO_TICKS(50));

        while (s_log_used > 0) {
            size_t to_read = s_log_used;
            if (to_read > sizeof(chunk)) to_read = sizeof(chunk);

            for (size_t i = 0; i < to_read; ++i) {
                chunk[i] = s_log_buf[s_log_tail];
                s_log_tail = (s_log_tail + 1) % LOG_BUF_SIZE;
            }
            s_log_used -= to_read;

            uart_write_bytes(cfg::DEBUG_UART_PORT, chunk, to_read);
        }
    }
}

static int debug_vprintf(const char* fmt, va_list args)
{
    char buf[LOG_MSG_MAX];
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    if (len > 0) {
        log_write(buf, len);
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

    s_log_sem = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(log_task, "log", 2048, nullptr, 1, nullptr, 1);

    esp_log_set_vprintf(debug_vprintf);
}
#else
static void debug_uart_init() {}
#endif

static void log_hex(const char* tag, const char* prefix,
                     const uint8_t* data, size_t len)
{
#if DEBUG_VERBOSE
    char hex[1024];
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 3 < sizeof(hex); ++i) {
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02x", data[i]);
    }
    ESP_LOGD(tag, "%s [%zu]: %s", prefix, len, hex);
#else
    (void)tag; (void)prefix; (void)data; (void)len;
#endif
}

static void log_hex_line(const char* tag, const char* dir,
                          const uint8_t* data, size_t len)
{
#if DEBUG_LOGS
    char hex[1024];
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 3 < sizeof(hex); ++i) {
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02x", data[i]);
    }
    ESP_LOGI(tag, "%s [%zu] %s", dir, len, hex);
#else
    (void)tag; (void)dir; (void)data; (void)len;
#endif
}

// ── raw capture buffer for tests ───────────────────────────────
#if DEBUG_VERBOSE
static uint8_t  s_capture_buf[4096];
static size_t   s_capture_len = 0;
#endif

static void capture_raw(uint8_t byte)
{
#if DEBUG_VERBOSE
    if (s_capture_len < sizeof(s_capture_buf)) {
        s_capture_buf[s_capture_len++] = byte;
    }
#else
    (void)byte;
#endif
}

static void dump_capture()
{
#if DEBUG_VERBOSE
    if (s_capture_len == 0) return;
    LOGD("=== RAW UART %zu bytes ===", s_capture_len);
    log_hex(TAG, "raw", s_capture_buf, s_capture_len);
    LOGD("=== END RAW ===");
    s_capture_len = 0;
#endif
}

// ── statistics ─────────────────────────────────────────────────
struct Stats {
    uint32_t tx_frames;       // KISS frames sent to ESP-NOW
    uint32_t tx_frags;        // individual fragments sent
    uint32_t tx_send_fail;    // esp_now_send() returned error
    uint32_t tx_frag_fail;    // send_broadcast() returned false
    uint32_t tx_uart_drop;    // KISS frames dropped (UART buffer overflow)
    uint32_t rx_frames;       // complete reassembled frames from ESP-NOW
    uint32_t rx_frags;        // individual fragments received
    uint32_t rx_queue_full;   // RX queue overflow
    uint32_t rx_reasm_ovf;    // reassembly buffer overflow
    uint32_t rx_bad_len;      // ESP-NOW RX bad length
    uint32_t rx_bad_hdr;      // bad fragment header
    uint32_t rx_bad_crc;      // CRC16 mismatch
    uint32_t uart_buf_peak;   // max UART RX buffer level seen (bytes)
};

static Stats s_stats = {};
static uint32_t s_last_stats_tick = 0;
static constexpr uint32_t STATS_INTERVAL_MS = 10000;  // dump every 10s

static void stats_dump()
{
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (now - s_last_stats_tick < STATS_INTERVAL_MS) return;
    s_last_stats_tick = now;

    LOGI("=== STATS (last %lus) ===", STATS_INTERVAL_MS / 1000);
    LOGI("  TX: frames=%lu frags=%lu send_fail=%lu frag_fail=%lu uart_drop=%lu",
         s_stats.tx_frames, s_stats.tx_frags,
         s_stats.tx_send_fail, s_stats.tx_frag_fail, s_stats.tx_uart_drop);
    LOGI("  RX: frames=%lu frags=%lu q_full=%lu reasm_ovf=%lu bad_len=%lu bad_hdr=%lu bad_crc=%lu",
         s_stats.rx_frames, s_stats.rx_frags,
         s_stats.rx_queue_full, s_stats.rx_reasm_ovf,
         s_stats.rx_bad_len, s_stats.rx_bad_hdr, s_stats.rx_bad_crc);
    LOGI("  UART RX buf peak: %lu bytes (buf_size=%d)",
         s_stats.uart_buf_peak, cfg::UART_BUF_SIZE);
    s_stats.uart_buf_peak = 0;
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

// ── CRC16 (CCITT, poly=0x1021, init=0xFFFF) ────────────────────
static uint16_t crc16_ccitt(const uint8_t* data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

// ── fragmentation ──────────────────────────────────────────────
static constexpr uint8_t FRAG_MORE = 0x80;

static void send_fragmented(const uint8_t* data, size_t len)
{
    s_radio.drain_send();

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

        LOGD("  frag hdr=0x%02x offset=%zu/%zu chunk=%zu",
                 header, offset, len, chunk);

        bool ok = s_radio.send_broadcast(pkt, 1 + chunk);
        if (!ok) {
            LOGW("frag send FAILED, frame dropped");
            s_stats.tx_frag_fail++;
            return;
        }

        s_stats.tx_frags++;
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
#if DEBUG_VERBOSE
        s_capture_len = 0;
#endif

        for (;;) {
            int byte = s_uart.read_byte();
            if (byte < 0) continue;

            capture_raw(static_cast<uint8_t>(byte));

            if (codec.decode_feed(static_cast<uint8_t>(byte))) {
                break;
            }
        }

        dump_capture();

        // check UART RX buffer level
        size_t uart_buf_level = 0;
        if (uart_get_buffered_data_len(cfg::UART_PORT, &uart_buf_level) == ESP_OK) {
            if (uart_buf_level > s_stats.uart_buf_peak) {
                s_stats.uart_buf_peak = uart_buf_level;
            }
            if (uart_buf_level > cfg::UART_BUF_SIZE * 70 / 100) {
                LOGW("UART RX buf %zu/%d bytes (%zu%%)",
                     uart_buf_level, cfg::UART_BUF_SIZE,
                     uart_buf_level * 100 / cfg::UART_BUF_SIZE);
            }
        }

        const KissFrame& f = codec.frame();

        LOGD("KISS frame: cmd=0x%02x payload_len=%zu",
                 f.command, f.payload_len);

        if (f.payload_len > 0) {
            log_hex_line(TAG, "KISS TX", f.payload, f.payload_len);
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

        uint16_t crc = crc16_ccitt(f.payload, f.payload_len);
        uint8_t frame_with_crc[cfg::KISS_MAX_FRAME + 2];
        memcpy(frame_with_crc, f.payload, f.payload_len);
        frame_with_crc[f.payload_len]     = crc >> 8;
        frame_with_crc[f.payload_len + 1] = crc & 0xFF;
        size_t frame_len = f.payload_len + 2;

        LOGI("TX %zu bytes via ESP-NOW (%s)",
                 frame_len,
                 (frame_len > cfg::FRAG_MAX_DATA) ? "fragmented" : "single");

        log_hex_line(TAG, "TX", frame_with_crc, frame_len);

        send_fragmented(frame_with_crc, frame_len);
        s_stats.tx_frames++;

        stats_dump();

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
        s_stats.rx_bad_len++;
        return;
    }

    uint8_t header = data[0];
    bool more = (header & FRAG_MORE) != 0;
    const uint8_t* payload = data + 1;
    size_t payload_len = len - 1;

    LOGD("ESP-NOW RX cb: %02x:%02x:%02x:%02x:%02x:%02x len=%zu hdr=0x%02x RSSI=%d",
             src_mac[0], src_mac[1], src_mac[2],
             src_mac[3], src_mac[4], src_mac[5],
             len, header, rssi);

    s_stats.rx_frags++;

    RxPacket pkt;
    memcpy(pkt.src_mac, src_mac, 6);
    pkt.rssi = rssi;

    static uint8_t reasm_buf[cfg::KISS_MAX_FRAME + 2];
    static size_t  reasm_len = 0;

    if (!more) {
        // last fragment or single — combine with reassembly buffer
        size_t total = reasm_len + payload_len;
        if (total > cfg::KISS_MAX_FRAME + 2) {
            LOGW("reassembly overflow (%zu), dropping", total);
            s_stats.rx_reasm_ovf++;
            reasm_len = 0;
            return;
        }
        memcpy(reasm_buf + reasm_len, payload, payload_len);
        reasm_len = 0;

        if (total < 3) {
            LOGW("frame too short for CRC (%zu), dropping", total);
            s_stats.rx_bad_crc++;
            return;
        }

        size_t data_len = total - 2;
        uint16_t recv_crc = ((uint16_t)reasm_buf[data_len] << 8) | reasm_buf[data_len + 1];
        uint16_t calc_crc = crc16_ccitt(reasm_buf, data_len);

        if (recv_crc != calc_crc) {
            LOGW("CRC16 mismatch: recv=0x%04x calc=0x%04x len=%zu, dropping",
                 recv_crc, calc_crc, data_len);
            s_stats.rx_bad_crc++;
            return;
        }

        pkt.len = static_cast<uint16_t>(data_len);
        memcpy(pkt.data, reasm_buf, data_len);

        if (xQueueSend(s_rx_queue, &pkt, 0) != pdTRUE) {
            LOGW("RX queue FULL, dropping");
            s_stats.rx_queue_full++;
        } else {
            s_stats.rx_frames++;
        }
    } else {
        // middle fragment — append to reassembly buffer
        if (reasm_len + payload_len <= cfg::KISS_MAX_FRAME) {
            memcpy(reasm_buf + reasm_len, payload, payload_len);
            reasm_len += payload_len;
            LOGD("  frag stored: %zu (total %zu)", payload_len, reasm_len);
        } else {
            LOGW("  reassembly overflow, dropping");
            s_stats.rx_reasm_ovf++;
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

        LOGD("ESP-NOW RX: from %02x:%02x:%02x:%02x:%02x:%02x RSSI=%d len=%u",
                 pkt.src_mac[0], pkt.src_mac[1], pkt.src_mac[2],
                 pkt.src_mac[3], pkt.src_mac[4], pkt.src_mac[5],
                 pkt.rssi, pkt.len);

        log_hex(TAG, "ESP-NOW payload", pkt.data, pkt.len);

        log_hex_line(TAG, "KISS RX", pkt.data, pkt.len);

        size_t encoded = KissCodec::encode(pkt.data, pkt.len,
                                           kiss_buf, sizeof(kiss_buf));
        if (encoded > 0) {
            log_hex(TAG, "KISS encoded", kiss_buf, encoded);

            s_uart.write(kiss_buf, encoded);
            LOGD("UART write %zu bytes", encoded);

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
    LOGI("Stats dump every %lu ms", STATS_INTERVAL_MS);
#endif

    s_rx_queue = xQueueCreate(cfg::QUEUE_SIZE, sizeof(RxPacket));
    assert(s_rx_queue);

    s_uart.init();
    s_radio.init();
    s_radio.set_recv_callback(on_radio_recv);

    xTaskCreatePinnedToCore(uart_rx_task,   "uart_rx",   cfg::TASK_STACK, nullptr, cfg::TASK_PRIO, nullptr, 1);
    xTaskCreatePinnedToCore(espnow_rx_task, "espnow_rx", cfg::TASK_STACK, nullptr, cfg::TASK_PRIO, nullptr, 0);

    LOGI("modem running");
}
