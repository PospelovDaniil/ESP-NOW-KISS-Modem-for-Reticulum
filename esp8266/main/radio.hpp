#pragma once

#include <cstdint>
#include <functional>

#include "config.hpp"
#include "esp_now.h"

using RadioRecvCallback = std::function<void(const uint8_t* src_mac,
                                             int8_t rssi,
                                             const uint8_t* data,
                                             size_t len)>;

class Radio {
public:
    bool init();
    bool send_broadcast(const uint8_t* data, size_t len);
    void set_recv_callback(RadioRecvCallback cb) { recv_cb_ = std::move(cb); }
    void get_own_mac(uint8_t* mac) const;

private:
    static void esp_now_recv_handler(const uint8_t* mac_addr,
                                     const uint8_t* data, int data_len);
    static void esp_now_send_handler(const uint8_t* mac_addr,
                                     esp_now_send_status_t status);

    static Radio* instance_;
    RadioRecvCallback recv_cb_;
    uint8_t own_mac_[6] = {};
};
