#pragma once

#include <cstdint>
#include <cstddef>

using RadioRecvCallback = void (*)(const uint8_t *src_mac, const uint8_t *data, size_t len);

int  radio_init(void);
int  radio_send_broadcast(const uint8_t *data, size_t len);
void radio_set_recv_callback(RadioRecvCallback cb);
void radio_get_own_mac(uint8_t *mac);
