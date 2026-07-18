#pragma once

#include <cstdint>
#include <cstddef>

int  uart_bridge_init(void);
int  uart_bridge_read_byte(void);
int  uart_bridge_write(const uint8_t *data, size_t len);
