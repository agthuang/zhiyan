#include "radio.h"

#include "esp_mac.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

static const char *TAG = "radio";
static const char *NVS_NS = "vibe";
static const char *NVS_PEER = "peer";

static radio_rx_cb_t s_cb;
static void *s_ctx;
static uint8_t s_peer[6];
static bool s_have_peer;
static uint16_t s_seq;
static SemaphoreHandle_t s_tx_sem;
static volatile bool s_tx_busy;

static bool s_modem_sleep;
static bool s_boosted;

/* ESP-NOW connectionless PS (no AP). Needs CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE. */
#define ECO_WAKE_INTERVAL_MS 400
#define ECO_WAKE_WINDOW_MS   160

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
    /* Default always-on RF; eco uses connectionless wake window after esp_now_init. */
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

static void add_peer(const uint8_t mac[6], bool broadcast)
{
    if (esp_now_is_peer_exist(mac)) {
        return;
    }
    esp_now_peer_info_t p = {0};
    memcpy(p.peer_addr, mac, 6);
    p.channel = VK_ESPNOW_CHANNEL;
    p.encrypt = false;
    p.ifidx = WIFI_IF_STA;
    if (broadcast) {
        p.peer_addr[0] = 0xFF;
        p.peer_addr[1] = 0xFF;
        p.peer_addr[2] = 0xFF;
        p.peer_addr[3] = 0xFF;
        p.peer_addr[4] = 0xFF;
        p.peer_addr[5] = 0xFF;
    }
    esp_err_t err = esp_now_add_peer(&p);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGW(TAG, "add_peer failed %s", esp_err_to_name(err));
    }
}

static void on_send(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    (void)mac_addr;
    (void)status;
    s_tx_busy = false;
    if (s_tx_sem) {
        xSemaphoreGive(s_tx_sem);
    }
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

void radio_init(radio_rx_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_ctx = ctx;
    s_tx_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(s_tx_sem);

    wifi_init_sta();
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_send));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));

    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    add_peer(bcast, true);

    load_peer();
    if (s_have_peer) {
        add_peer(s_peer, false);
        ESP_LOGI(TAG, "peer " MACSTR, MAC2STR(s_peer));
    } else {
        ESP_LOGI(TAG, "no peer — will pair");
    }
}

void radio_set_modem_sleep(bool enable)
{
    s_modem_sleep = enable;
    if (s_boosted) {
        (void)esp_wifi_force_wakeup_release();
        s_boosted = false;
    }
    if (enable) {
        /*
         * Pure ESP-NOW (no AP): WIFI_PS_MIN_MODEM alone does almost nothing.
         * Connectionless wake interval + ESP-NOW window is the real saver
         * (needs CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE — already on).
         */
        (void)esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        esp_err_t e1 = esp_wifi_connectionless_module_set_wake_interval(ECO_WAKE_INTERVAL_MS);
        esp_err_t e2 = esp_now_set_wake_window(ECO_WAKE_WINDOW_MS);
        ESP_LOGI(TAG, "espnow eco PS interval=%ums window=%ums (%s/%s)",
                 (unsigned)ECO_WAKE_INTERVAL_MS, (unsigned)ECO_WAKE_WINDOW_MS,
                 esp_err_to_name(e1), esp_err_to_name(e2));
    } else {
        (void)esp_wifi_connectionless_module_set_wake_interval(
            ESP_WIFI_CONNECTIONLESS_INTERVAL_DEFAULT_MODE);
        (void)esp_now_set_wake_window(65535);
        (void)esp_wifi_set_ps(WIFI_PS_NONE);
        ESP_LOGI(TAG, "espnow PS off (always RX)");
    }
}

void radio_modem_boost(void)
{
    if (!s_modem_sleep || s_boosted) {
        return;
    }
    /* Keep RF up for Voice TX; ignore listen-window sleeping. */
    if (esp_wifi_force_wakeup_acquire() != ESP_OK) {
        return;
    }
    (void)esp_now_set_wake_window(65535);
    s_boosted = true;
}

void radio_modem_unboost(void)
{
    if (!s_modem_sleep || !s_boosted) {
        return;
    }
    (void)esp_wifi_force_wakeup_release();
    (void)esp_now_set_wake_window(ECO_WAKE_WINDOW_MS);
    s_boosted = false;
}

bool radio_has_peer(void)
{
    return s_have_peer;
}

void radio_get_peer(uint8_t mac[6])
{
    memcpy(mac, s_peer, 6);
}

void radio_clear_peer(void)
{
    if (s_have_peer) {
        esp_now_del_peer(s_peer);
    }
    s_have_peer = false;
    memset(s_peer, 0, 6);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_PEER);
        nvs_commit(h);
        nvs_close(h);
    }
}

void radio_set_peer(const uint8_t mac[6])
{
    memcpy(s_peer, mac, 6);
    s_have_peer = true;
    add_peer(mac, false);
    save_peer(mac);
    ESP_LOGI(TAG, "paired " MACSTR, MAC2STR(mac));
}

bool radio_tx_ready(void)
{
    return !s_tx_busy;
}

bool radio_send(vk_pkt_type_t type, const void *payload, uint8_t len)
{
    uint8_t dst[6];
    if (type == VK_PKT_PAIR_REQ) {
        memset(dst, 0xFF, 6);
    } else if (s_have_peer) {
        memcpy(dst, s_peer, 6);
    } else {
        return false;
    }

    uint8_t buf[VK_ESPNOW_MAX];
    size_t n = vk_pack(buf, sizeof(buf), type, s_seq++, payload, len);
    if (n == 0) {
        return false;
    }

    if (xSemaphoreTake(s_tx_sem, pdMS_TO_TICKS(30)) != pdTRUE) {
        return false;
    }
    s_tx_busy = true;
    esp_err_t err = esp_now_send(dst, buf, n);
    if (err != ESP_OK) {
        s_tx_busy = false;
        xSemaphoreGive(s_tx_sem);
        return false;
    }
    return true;
}

bool radio_send_audio(const int16_t *samples, size_t count)
{
    if (!s_have_peer || samples == NULL || count == 0) {
        return false;
    }
    if (count > VK_AUDIO_SAMPLES) {
        count = VK_AUDIO_SAMPLES;
    }
    /* Short wait — prefer slight delay over hard-dropping every busy tick. */
    if (s_tx_busy) {
        if (xSemaphoreTake(s_tx_sem, pdMS_TO_TICKS(8)) != pdTRUE) {
            return false;
        }
        xSemaphoreGive(s_tx_sem);
    }
    return radio_send(VK_PKT_AUDIO, samples, (uint8_t)(count * sizeof(int16_t)));
}

void radio_send_pair_req(void)
{
    vk_pair_req_t req = {0};
    esp_wifi_get_mac(WIFI_IF_STA, req.mac);
    req.ver = VK_PROTO_VER;
    radio_send(VK_PKT_PAIR_REQ, &req, sizeof(req));
}
