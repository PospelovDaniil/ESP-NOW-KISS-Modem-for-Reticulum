#include "uart_bridge.hpp"
#include "app_config.h"

#include <cstring>

#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "driver/uart.h"

int uart_bridge_init(void)
{
    uart_config_t uart_cfg;
    std::memset(&uart_cfg, 0, sizeof(uart_cfg));
    uart_cfg.baud_rate  = KISS_UART_BAUD;
    uart_cfg.data_bits  = UART_DATA_8_BITS;
    uart_cfg.parity     = UART_PARITY_DISABLE;
    uart_cfg.stop_bits  = UART_STOP_BITS_1;
    uart_cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
    uart_cfg.rx_flow_ctrl_thresh = 0;

    esp_err_t err;
    err = uart_param_config(UART_NUM_0, &uart_cfg);
    if (err != ESP_OK) return -1;

    err = uart_driver_install(UART_NUM_0, KISS_UART_BUF_SIZE, KISS_UART_BUF_SIZE, 0, nullptr, 0);
    if (err != ESP_OK) return -1;

    return 0;
}

int uart_bridge_read_byte(void)
{
    uint8_t byte;
    int len = uart_read_bytes(UART_NUM_0, &byte, 1, 100 / portTICK_PERIOD_MS);
    return (len == 1) ? static_cast<int>(byte) : -1;
}

int uart_bridge_write(const uint8_t *data, size_t len)
{
    int written = uart_write_bytes(UART_NUM_0, reinterpret_cast<const char*>(data), len);
    return (written == static_cast<int>(len)) ? 0 : -1;
}
