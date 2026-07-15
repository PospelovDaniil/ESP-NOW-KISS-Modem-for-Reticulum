#pragma once

#include <cstdint>
#include <array>

#include "driver/uart.h"
#include "esp_wifi.h"

// Debug — set to 0 to disable (compiled out, max speed)
#ifndef DEBUG_LOGS
#define DEBUG_LOGS  0
#endif
#ifndef DEBUG_LED
#define DEBUG_LED   0
#endif

namespace cfg {

// UART — KISS (USB)
inline constexpr uart_port_t UART_PORT       = UART_NUM_0;
inline constexpr int         UART_BAUD       = 115200;
inline constexpr int         UART_TX_PIN     = 1;   // GPIO1
inline constexpr int         UART_RX_PIN     = 3;   // GPIO3
inline constexpr int         UART_BUF_SIZE   = 8192;

// UART — Debug
inline constexpr uart_port_t DEBUG_UART_PORT = UART_NUM_2;
inline constexpr int         DEBUG_UART_BAUD = 921600;
inline constexpr int         DEBUG_TX_PIN    = 17;  // GPIO17
inline constexpr int         DEBUG_RX_PIN    = 16;  // GPIO16

// ESP-NOW
inline constexpr uint8_t     BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
inline constexpr int         WIFI_CHANNEL     = 13;
inline constexpr int         ESPNOW_ENCRYPT   = false;
inline constexpr int         WIFI_TX_POWER_DB8 = 80; // 0.25 dBm units: 80=20dBm (max), 32=8dBm
inline constexpr size_t      ESPNOW_MAX_PAYLOAD = 250;
inline constexpr size_t      FRAG_HEADER_SIZE  = 1;    // 1 byte fragment header
inline constexpr size_t      FRAG_MAX_DATA     = ESPNOW_MAX_PAYLOAD - FRAG_HEADER_SIZE; // 249

// KISS
inline constexpr uint8_t     KISS_FEND  = 0xC0;
inline constexpr uint8_t     KISS_FESC  = 0xDB;
inline constexpr uint8_t     KISS_TFEND = 0xDC;
inline constexpr uint8_t     KISS_TFESC = 0xDD;
inline constexpr uint8_t     KISS_CMD_DATA    = 0x00;
inline constexpr uint8_t     KISS_CMD_RETURN  = 0xFF;
inline constexpr size_t      KISS_MAX_FRAME   = 500;  // matches RNS MTU

// LED
inline constexpr int         LED_GPIO        = 2;

// FreeRTOS
inline constexpr uint32_t    TASK_STACK       = 8192;
inline constexpr UBaseType_t TASK_PRIO        = 5;
inline constexpr UBaseType_t QUEUE_SIZE       = 8;

}  // namespace cfg
