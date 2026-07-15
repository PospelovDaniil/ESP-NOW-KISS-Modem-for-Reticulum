#pragma once

#include <cstdint>
#include <cstddef>

#include "config.hpp"

class UartBridge {
public:
    bool init();

    int  read_byte();
    bool write(const uint8_t* data, size_t len);
};
