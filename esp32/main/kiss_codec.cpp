#include "kiss_codec.hpp"

#include <cstring>

void KissCodec::reset()
{
    state_    = State::SEARCHING;
    pos_      = 0;
    has_cmd_  = false;
    frame_.command     = 0;
    frame_.payload_len = 0;
}

bool KissCodec::decode_begin()
{
    reset();
    return true;
}

bool KissCodec::decode_feed(uint8_t byte)
{
    switch (state_) {

    case State::SEARCHING:
        if (byte == cfg::KISS_FEND) {
            state_   = State::IN_FRAME;
            pos_     = 0;
            has_cmd_ = false;
        }
        break;

    case State::IN_FRAME:
        if (byte == cfg::KISS_FEND) {
            if (has_cmd_) {
                frame_.payload_len = pos_;
                return true;   // complete frame received
            }
            break;             // consecutive FENDs — stay in IN_FRAME
        }
        if (byte == cfg::KISS_FESC) {
            state_ = State::ESCAPE;
            break;
        }
        if (!has_cmd_) {
            frame_.command = byte;
            has_cmd_ = true;
            break;
        }
        if (pos_ < cfg::KISS_MAX_FRAME) {
            frame_.payload[pos_++] = byte;
        }
        break;

    case State::ESCAPE:
        if (byte == cfg::KISS_TFEND) {
            byte = cfg::KISS_FEND;
        } else if (byte == cfg::KISS_TFESC) {
            byte = cfg::KISS_FESC;
        }
        if (pos_ < cfg::KISS_MAX_FRAME) {
            frame_.payload[pos_++] = byte;
        }
        state_ = State::IN_FRAME;
        break;
    }

    return false;
}

size_t KissCodec::encode(const uint8_t* src, size_t src_len,
                         uint8_t* dst, size_t dst_cap)
{
    size_t i = 0;

    if (i < dst_cap) dst[i++] = cfg::KISS_FEND;
    if (i < dst_cap) dst[i++] = cfg::KISS_CMD_DATA;

    for (size_t n = 0; n < src_len; ++n) {
        uint8_t b = src[n];
        if (b == cfg::KISS_FEND) {
            if (i < dst_cap) dst[i++] = cfg::KISS_FESC;
            if (i < dst_cap) dst[i++] = cfg::KISS_TFEND;
        } else if (b == cfg::KISS_FESC) {
            if (i < dst_cap) dst[i++] = cfg::KISS_FESC;
            if (i < dst_cap) dst[i++] = cfg::KISS_TFESC;
        } else {
            if (i < dst_cap) dst[i++] = b;
        }
    }

    if (i < dst_cap) dst[i++] = cfg::KISS_FEND;
    return i;
}
