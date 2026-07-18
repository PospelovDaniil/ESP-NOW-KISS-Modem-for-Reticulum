#pragma once

#include <cstdint>

#define KISS_FEND           0xC0
#define KISS_FESC           0xDB
#define KISS_TFEND          0xDC
#define KISS_TFESC          0xDD
#define KISS_CMD_DATA       0x00
#define KISS_CMD_RETURN     0xFF
#define KISS_MAX_FRAME      500

#define ESPNOW_MAX_PAYLOAD  250
#define FRAG_HEADER_SIZE    1
#define FRAG_MAX_DATA       (ESPNOW_MAX_PAYLOAD - FRAG_HEADER_SIZE)

#define ESPNOW_CHANNEL      13
#define ESPNOW_ENCRYPT      0

#define KISS_UART_BAUD      57600
#define KISS_UART_BUF_SIZE  4096

#define QUEUE_SIZE          10
#define TASK_STACK          4096
#define TASK_PRIO           5
