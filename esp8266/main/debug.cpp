#include "debug.h"

#include <cstring>
#include <cstdio>
#include <cstdarg>

#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "driver/uart.h"

#define DBG_UART   UART_NUM_1
#define DBG_BAUD   115200

void debug_init(void)
{
    uart_config_t cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.baud_rate = DBG_BAUD;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity    = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;

    uart_driver_install(DBG_UART, 0, 512, 0, nullptr, 0);
    uart_param_config(DBG_UART, &cfg);
}

void debug_printf(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len > 0) {
        uart_write_bytes(DBG_UART, buf, len);
    }
}

void debug_hex(const char *prefix, const uint8_t *data, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    char buf[300];
    size_t plen = std::strlen(prefix);
    if (plen > 60) plen = 60;
    std::memcpy(buf, prefix, plen);
    size_t pos = plen;
    if (pos < sizeof(buf) - 1) buf[pos++] = ' ';
    for (size_t i = 0; i < len && pos + 2 < sizeof(buf); i++) {
        buf[pos++] = hex[data[i] >> 4];
        buf[pos++] = hex[data[i] & 0x0F];
    }
    if (pos < sizeof(buf) - 1) buf[pos++] = '\n';
    uart_write_bytes(DBG_UART, buf, pos);
}
