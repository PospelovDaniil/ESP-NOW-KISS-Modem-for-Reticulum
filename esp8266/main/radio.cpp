#include "radio.hpp"
#include "app_config.h"
#include "debug.h"

#include <cstring>

#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "nvs_flash.h"

static RadioRecvCallback s_recv_cb = nullptr;
static uint8_t s_own_mac[6] = {};
static SemaphoreHandle_t s_send_sem = nullptr;

static void esp_now_recv_handler(const uint8_t *mac_addr,
                                 const uint8_t *data, int data_len)
{
    debug_printf("[RADIO] ** RECV CB HIT ** len=%d cb=%d\n", data_len, (s_recv_cb != nullptr));
    if (!mac_addr || !data || data_len <= 0) {
        debug_printf("[RADIO] recv cb: bad args\n");
        return;
    }
    debug_printf("[RADIO] from %02x:%02x:%02x:%02x:%02x:%02x\n",
                 mac_addr[0], mac_addr[1], mac_addr[2],
                 mac_addr[3], mac_addr[4], mac_addr[5]);
    if (std::memcmp(mac_addr, s_own_mac, 6) == 0) {
        debug_printf("[RADIO] self-match, drop\n");
        return;
    }
    if (!s_recv_cb) {
        debug_printf("[RADIO] no cb registered, drop\n");
        return;
    }

    debug_printf("[RADIO] recv %d bytes\n", data_len);
    debug_hex("[RADIO] rx", data, data_len);

    s_recv_cb(mac_addr, data, static_cast<size_t>(data_len));
}

static void esp_now_send_handler(const uint8_t *mac_addr,
                                 esp_now_send_status_t status)
{
    debug_printf("[RADIO] send cb: status=%d\n", status);
    if (s_send_sem) {
        xSemaphoreGive(s_send_sem);
    }
}

int radio_init(void)
{
    esp_err_t err;

    debug_printf("[RADIO] init start\n");

    err = nvs_flash_init();
    if (err != ESP_OK) { debug_printf("[RADIO] nvs fail: %d\n", err); return -1; }

    err = esp_netif_init();
    if (err != ESP_OK) { debug_printf("[RADIO] netif fail: %d\n", err); return -1; }

    err = esp_event_loop_create_default();
    if (err != ESP_OK) { debug_printf("[RADIO] event fail: %d\n", err); return -1; }

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_cfg);
    if (err != ESP_OK) { debug_printf("[RADIO] wifi_init fail: %d\n", err); return -1; }

    debug_printf("[RADIO] wifi_init OK\n");

    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    err = esp_wifi_start();
    debug_printf("[RADIO] wifi_start: %d\n", err);

    uint8_t channel = (ESPNOW_CHANNEL >= 1 && ESPNOW_CHANNEL <= 13) ? ESPNOW_CHANNEL : 6;
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    uint8_t ch = 0;
    esp_wifi_get_channel(&ch, nullptr);
    debug_printf("[RADIO] channel set=%d get=%d\n", channel, ch);

    esp_wifi_set_protocol(WIFI_IF_STA,
                          WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G |
                          WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);

    uint8_t prot = 0;
    esp_wifi_get_protocol(WIFI_IF_STA, &prot);
    debug_printf("[RADIO] protocol get=0x%02x\n", prot);

    err = esp_now_init();
    if (err != ESP_OK) { debug_printf("[RADIO] esp_now_init fail: %d\n", err); return -1; }
    debug_printf("[RADIO] esp_now_init OK\n");

    esp_now_register_recv_cb(esp_now_recv_handler);
    esp_now_register_send_cb(esp_now_send_handler);

    s_send_sem = xSemaphoreCreateBinary();

    esp_wifi_get_mac(WIFI_IF_STA, s_own_mac);
    debug_printf("[RADIO] MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
                 s_own_mac[0], s_own_mac[1], s_own_mac[2],
                 s_own_mac[3], s_own_mac[4], s_own_mac[5]);

    esp_now_peer_info_t peer;
    std::memset(&peer, 0, sizeof(peer));
    std::memcpy(peer.peer_addr, "\xff\xff\xff\xff\xff\xff", 6);
    peer.channel = channel;
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = ESPNOW_ENCRYPT;

    err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        debug_printf("[RADIO] add_peer fail: %d\n", err);
        return -1;
    }

    debug_printf("[RADIO] init done\n");
    return 0;
}

int radio_send_broadcast(const uint8_t *data, size_t len)
{
    static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_err_t err = esp_now_send(bcast, data, len);
    if (err != ESP_OK) return -1;

    if (xSemaphoreTake(s_send_sem, pdMS_TO_TICKS(100)) != pdTRUE) {
        return -1;
    }
    return 0;
}

void radio_set_recv_callback(RadioRecvCallback cb)
{
    s_recv_cb = cb;
}

void radio_get_own_mac(uint8_t *mac)
{
    std::memcpy(mac, s_own_mac, 6);
}
