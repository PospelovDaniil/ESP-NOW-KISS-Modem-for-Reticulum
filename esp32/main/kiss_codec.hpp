#pragma once

#include <cstddef>
#include <cstdint>

#include "config.hpp"

struct KissFrame {
    uint8_t  command;
    uint8_t  payload[cfg::KISS_MAX_FRAME];
    size_t   payload_len;
};

class KissCodec {
public:
    enum class State { SEARCHING, IN_FRAME, ESCAPE };

    KissCodec() = default;

    void reset();

    bool decode_begin();
    bool decode_feed(uint8_t byte);

    const KissFrame& frame() const { return frame_; }

    static size_t encode(const uint8_t* src, size_t src_len,
                         uint8_t* dst, size_t dst_cap);

private:
    State  state_ = State::SEARCHING;
    size_t pos_   = 0;
    bool   has_cmd_ = false;
    KissFrame frame_{};
};
