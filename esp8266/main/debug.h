#pragma once

#include <cstdint>
#include <cstddef>

void debug_init(void);
void debug_printf(const char *fmt, ...);
void debug_hex(const char *prefix, const uint8_t *data, size_t len);
