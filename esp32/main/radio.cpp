#include "radio.hpp"

#include <cstring>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

static const char* TAG = "radio";

Radio* Radio::instance_ = nullptr;

void Radio::esp_now_recv_handler(const esp_now_recv_info_t* info,
                                 const uint8_t* data, int data_len)
{
    if (!instance_ || !instance_->recv_cb_) return;
    if (data_len <= 0) return;

    const uint8_t* src = info->src_addr;
    if (std::memcmp(src, instance_->own_mac_, 6) == 0) return;

    int8_t rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
    instance_->recv_cb_(src, rssi,
                        reinterpret_cast<const uint8_t*>(data),
                        static_cast<size_t>(data_len));
}

void Radio::esp_now_send_handler(const esp_now_send_info_t* tx_info,
                                 esp_now_send_status_t status)
{
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "send failed");
    }
    if (instance_ && instance_->send_sem_) {
        xSemaphoreGive(instance_->send_sem_);
    }
}

bool Radio::init()
{
    instance_ = this;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_wifi_set_channel((cfg::WIFI_CHANNEL > 0 && cfg::WIFI_CHANNEL < 14 ? cfg::WIFI_CHANNEL : 6),
                          WIFI_SECOND_CHAN_NONE);

    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA,
                     WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));

    // TX power: 20 dBm (max), value in 0.25 dBm units
    esp_wifi_set_max_tx_power(cfg::WIFI_TX_POWER_DB);

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(esp_now_recv_handler));
    ESP_ERROR_CHECK(esp_now_register_send_cb(esp_now_send_handler));

    send_sem_ = xSemaphoreCreateBinary();

    esp_wifi_get_mac(WIFI_IF_STA, own_mac_);
    ESP_LOGI(TAG, "own MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             own_mac_[0], own_mac_[1], own_mac_[2],
             own_mac_[3], own_mac_[4], own_mac_[5]);

    esp_now_peer_info_t broadcast_peer = {};
    std::memcpy(broadcast_peer.peer_addr, cfg::BROADCAST_MAC, 6);
    broadcast_peer.channel = cfg::WIFI_CHANNEL;
    broadcast_peer.ifidx   = WIFI_IF_STA;
    broadcast_peer.encrypt = cfg::ESPNOW_ENCRYPT;

    esp_err_t err = esp_now_add_peer(&broadcast_peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGE(TAG, "add broadcast peer failed: %s", esp_err_to_name(err));
        return false;
    }

    esp_now_rate_config_t rate_cfg = {};
    rate_cfg.phymode = WIFI_PHY_MODE_LR;           // WIFI_PHY_MODE_LR / WIFI_PHY_MODE_11N / etc.
    rate_cfg.rate    = WIFI_PHY_RATE_1M_L;    // WIFI_PHY_RATE_LORA_500K / WIFI_PHY_RATE_MCS7_LGI / etc.
    rate_cfg.ersu    = false;
    rate_cfg.dcm     = false;
    esp_now_set_peer_rate_config(cfg::BROADCAST_MAC, &rate_cfg);
    ESP_LOGI(TAG, "ESP-NOW rate: phymode=%d rate=%d", rate_cfg.phymode, rate_cfg.rate);

    ESP_LOGI(TAG, "ESP-NOW ready");

    return true;
}

SendResult Radio::send_broadcast(const uint8_t* data, size_t len)
{
    esp_err_t err = esp_now_send(cfg::BROADCAST_MAC, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "send error: %s", esp_err_to_name(err));
        return SendResult::ERROR;
    }
    if (xSemaphoreTake(send_sem_, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "send timeout (callback not received)");
        return SendResult::TIMEOUT;
    }
    return SendResult::OK;
}

void Radio::drain_send()
{
    while (xSemaphoreTake(send_sem_, 0) == pdTRUE) {}
}

void Radio::get_own_mac(uint8_t* mac) const
{
    std::memcpy(mac, own_mac_, 6);
}
