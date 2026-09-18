#include "radio.h"

#include "esp_mac.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"

#include <string.h>

static const char *TAG = "radio";
static const char *NVS_NS = "vibe";
static const char *NVS_PEER = "peer";
#define PAIR_WINDOW_MS 45000

static rx_radio_cb_t s_cb;
static void *s_ctx;
static uint8_t s_peer[6];
static bool s_have_peer;
static bool s_pair_open = true;
static uint32_t s_boot_ms;
static uint16_t s_seq;

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(VK_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
}

static void load_peer(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = 6;
    if (nvs_get_blob(h, NVS_PEER, s_peer, &len) == ESP_OK && len == 6) {
        s_have_peer = true;
    }
    nvs_close(h);
}

static void save_peer(const uint8_t mac[6])
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_blob(h, NVS_PEER, mac, 6);
    nvs_commit(h);
    nvs_close(h);
}

static void add_peer(const uint8_t mac[6])
{
    if (esp_now_is_peer_exist(mac)) {
        return;
    }
    esp_now_peer_info_t p = {0};
    memcpy(p.peer_addr, mac, 6);
    p.channel = VK_ESPNOW_CHANNEL;
    p.encrypt = false;
    p.ifidx = WIFI_IF_STA;
    esp_now_add_peer(&p);
}

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!info || !data || len <= 0) {
        return;
    }
    vk_hdr_t hdr;
    const uint8_t *payload = NULL;
    if (!vk_parse(data, (size_t)len, &hdr, &payload)) {
        return;
    }
    if (s_cb) {
        s_cb(info->src_addr, &hdr, payload, s_ctx);
    }
}

void rx_radio_init(rx_radio_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_ctx = ctx;
    s_boot_ms = 0;
    wifi_init_sta();
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));

    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    add_peer(bcast);
    load_peer();
    if (s_have_peer) {
        add_peer(s_peer);
        ESP_LOGI(TAG, "peer " MACSTR, MAC2STR(s_peer));
    } else {
        ESP_LOGI(TAG, "unpaired — pairing open");
    }
    s_pair_open = true;
}

bool rx_radio_has_peer(void)
{
    return s_have_peer;
}

bool rx_radio_is_peer(const uint8_t mac[6])
{
    return s_have_peer && mac && memcmp(s_peer, mac, 6) == 0;
}

void rx_radio_get_peer(uint8_t mac[6])
{
    memcpy(mac, s_peer, 6);
}

void rx_radio_set_peer(const uint8_t mac[6])
{
    memcpy(s_peer, mac, 6);
    s_have_peer = true;
    add_peer(mac);
    save_peer(mac);
    s_pair_open = false;
    ESP_LOGI(TAG, "paired " MACSTR, MAC2STR(mac));
}

void rx_radio_clear_peer(void)
{
    if (s_have_peer) {
        esp_now_del_peer(s_peer);
    }
    s_have_peer = false;
    memset(s_peer, 0, 6);
    s_pair_open = true;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_PEER);
        nvs_commit(h);
        nvs_close(h);
    }
}

bool rx_radio_pairing_open(void)
{
    return s_pair_open || !s_have_peer;
}

void rx_radio_tick(uint32_t now_ms)
{
    if (s_boot_ms == 0) {
        s_boot_ms = now_ms ? now_ms : 1;
    }
    if (s_have_peer && s_pair_open && (now_ms - s_boot_ms) > PAIR_WINDOW_MS) {
        s_pair_open = false;
        ESP_LOGI(TAG, "pairing window closed");
    }
}

bool rx_radio_send(vk_pkt_type_t type, const void *payload, uint8_t len)
{
    if (!s_have_peer) {
        return false;
    }
    uint8_t buf[VK_ESPNOW_MAX];
    size_t n = vk_pack(buf, sizeof(buf), type, s_seq++, payload, len);
    if (n == 0) {
        return false;
    }
    return esp_now_send(s_peer, buf, n) == ESP_OK;
}
