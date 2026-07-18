#include "kiss_codec.hpp"
#include <cstring>

void KissCodec::reset()
{
    state_   = SEARCHING;
    pos_     = 0;
    has_cmd_ = false;
    frame_   = {};
}

void KissCodec::decode_begin()
{
    reset();
}

bool KissCodec::decode_feed(uint8_t byte)
{
    switch (state_) {

    case SEARCHING:
        if (byte == KISS_FEND) {
            state_   = IN_FRAME;
            pos_     = 0;
            has_cmd_ = false;
        }
        break;

    case IN_FRAME:
        if (byte == KISS_FEND) {
            if (has_cmd_) {
                frame_.payload_len = pos_;
                return true;
            }
            break;
        }
        if (byte == KISS_FESC) {
            state_ = ESCAPE;
            break;
        }
        if (!has_cmd_) {
            frame_.command = byte;
            has_cmd_ = true;
            break;
        }
        if (pos_ < KISS_MAX_FRAME) {
            frame_.payload[pos_++] = byte;
        }
        break;

    case ESCAPE:
        if (byte == KISS_TFEND) {
            byte = KISS_FEND;
        } else if (byte == KISS_TFESC) {
            byte = KISS_FESC;
        }
        if (pos_ < KISS_MAX_FRAME) {
            frame_.payload[pos_++] = byte;
        }
        state_ = IN_FRAME;
        break;
    }

    return false;
}

size_t KissCodec::encode(const uint8_t *src, size_t src_len,
                         uint8_t *dst, size_t dst_cap)
{
    size_t i = 0;

    if (i < dst_cap) dst[i++] = KISS_FEND;
    if (i < dst_cap) dst[i++] = KISS_CMD_DATA;

    for (size_t n = 0; n < src_len; ++n) {
        uint8_t b = src[n];
        if (b == KISS_FEND) {
            if (i < dst_cap) dst[i++] = KISS_FESC;
            if (i < dst_cap) dst[i++] = KISS_TFEND;
        } else if (b == KISS_FESC) {
            if (i < dst_cap) dst[i++] = KISS_FESC;
            if (i < dst_cap) dst[i++] = KISS_TFESC;
        } else {
            if (i < dst_cap) dst[i++] = b;
        }
    }

    if (i < dst_cap) dst[i++] = KISS_FEND;
    return i;
}
