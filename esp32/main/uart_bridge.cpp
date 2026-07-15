#include "uart_bridge.hpp"

#include "driver/uart.h"
#include "esp_log.h"

static const char* TAG = "uart";

bool UartBridge::init()
{
    const uart_config_t uart_cfg = {
        .baud_rate  = cfg::UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {},
    };

    ESP_ERROR_CHECK(uart_param_config(cfg::UART_PORT, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(cfg::UART_PORT,
                                 cfg::UART_TX_PIN, cfg::UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(cfg::UART_PORT,
                                        cfg::UART_BUF_SIZE,
                                        cfg::UART_BUF_SIZE,
                                        0, nullptr, 0));

    ESP_LOGI(TAG, "UART ready (baud=%d)", cfg::UART_BAUD);
    return true;
}

int UartBridge::read_byte()
{
    uint8_t byte;
    int len = uart_read_bytes(cfg::UART_PORT, &byte, 1, pdMS_TO_TICKS(100));
    return (len == 1) ? static_cast<int>(byte) : -1;
}

bool UartBridge::write(const uint8_t* data, size_t len)
{
    int written = uart_write_bytes(cfg::UART_PORT, data, len);
    return written == static_cast<int>(len);
}
