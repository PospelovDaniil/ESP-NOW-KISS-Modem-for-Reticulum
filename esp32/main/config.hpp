#pragma once

#include <cstdint>
#include <array>

#include "driver/uart.h"
#include "esp_wifi.h"

namespace cfg {

// UART
inline constexpr uart_port_t UART_PORT       = UART_NUM_0;
inline constexpr int         UART_BAUD       = 115200;
inline constexpr int         UART_TX_PIN     = 1;   // GPIO1
inline constexpr int         UART_RX_PIN     = 3;   // GPIO3
inline constexpr int         UART_BUF_SIZE   = 4096;

// ESP-NOW
inline constexpr uint8_t     BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
inline constexpr int         WIFI_CHANNEL     = 0;   // 0 = follow STA
inline constexpr int         ESPNOW_ENCRYPT   = false;

// KISS
inline constexpr uint8_t     KISS_FEND  = 0xC0;
inline constexpr uint8_t     KISS_FESC  = 0xDB;
inline constexpr uint8_t     KISS_TFEND = 0xDC;
inline constexpr uint8_t     KISS_TFESC = 0xDD;
inline constexpr uint8_t     KISS_CMD_DATA    = 0x00;
inline constexpr uint8_t     KISS_CMD_RETURN  = 0xFF;
inline constexpr size_t      KISS_MAX_FRAME   = 1500;

// FreeRTOS
inline constexpr uint32_t    TASK_STACK       = 4096;
inline constexpr UBaseType_t TASK_PRIO        = 5;
inline constexpr UBaseType_t QUEUE_SIZE       = 8;

}  // namespace cfg
