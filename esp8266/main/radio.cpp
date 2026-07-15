#include "radio.hpp"

#include <cstring>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "nvs_flash.h"

static const char* TAG = "radio";

Radio* Radio::instance_ = nullptr;

void Radio::esp_now_recv_handler(const uint8_t* mac_addr,
                                 const uint8_t* data, int data_len)
{
    if (!instance_ || !instance_->recv_cb_) return;
    if (data_len <= 0) return;

    if (std::memcmp(mac_addr, instance_->own_mac_, 6) == 0) return;

    instance_->recv_cb_(mac_addr, 0,
                        reinterpret_cast<const uint8_t*>(data),
                        static_cast<size_t>(data_len));
}

void Radio::esp_now_send_handler(const uint8_t* mac_addr,
                                 esp_now_send_status_t status)
{
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "send failed");
    }
}

bool Radio::init()
{
    instance_ = this;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_wifi_set_opmode(STATION_MODE));
    ESP_ERROR_CHECK(esp_wifi_start());

    if (cfg::WIFI_CHANNEL > 0) {
        esp_wifi_set_channel(cfg::WIFI_CHANNEL);
    }

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(esp_now_recv_handler));
    ESP_ERROR_CHECK(esp_now_register_send_cb(esp_now_send_handler));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    esp_wifi_get_mac(STATION_IF, own_mac_);
    ESP_LOGI(TAG, "own MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             own_mac_[0], own_mac_[1], own_mac_[2],
             own_mac_[3], own_mac_[4], own_mac_[5]);

    esp_now_peer_info_t broadcast_peer = {};
    std::memcpy(broadcast_peer.peer_addr, cfg::BROADCAST_MAC, 6);
    broadcast_peer.channel = cfg::WIFI_CHANNEL;
    broadcast_peer.ifidx   = STATION_IF;
    broadcast_peer.encrypt = cfg::ESPNOW_ENCRYPT;

    esp_err_t err = esp_now_add_peer(&broadcast_peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGE(TAG, "add broadcast peer failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "ESP-NOW ready");
    return true;
}

bool Radio::send_broadcast(const uint8_t* data, size_t len)
{
    esp_err_t err = esp_now_send(cfg::BROADCAST_MAC, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "send error: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

void Radio::get_own_mac(uint8_t* mac) const
{
    std::memcpy(mac, own_mac_, 6);
}
