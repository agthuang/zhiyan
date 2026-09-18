#include "audio_ring.h"
#include "radio.h"
#include "usb/vk_usb.h"

#include "vk_hid.h"
#include "vk_protocol.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <string.h>

static const char *TAG = "app";

typedef struct {
    uint8_t mac[6];
    vk_hdr_t hdr;
    uint8_t payload[VK_ESPNOW_MAX];
} rx_msg_t;

static QueueHandle_t s_q;      /* audio / telemetry / pair */
static QueueHandle_t s_q_key;  /* keys only — must not drop under audio flood */

static bool s_voice_held;
static uint32_t s_voice_held_ms;
static uint32_t s_last_voice_audio_ms;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void force_keys_up(void)
{
    vk_hid_report_t clear = {0};
    vk_usb_send_report(&clear);
    if (s_voice_held) {
        audio_ring_set_live(false);
        s_voice_held = false;
        vk_usb_cdc_printf("key 0 up\r\n"); /* mirror Voice release */
        ESP_LOGW(TAG, "force keys up (lost Voice UP?)");
    }
}

static void on_radio(const uint8_t mac[6], const vk_hdr_t *hdr, const uint8_t *payload, void *ctx)
{
    (void)ctx;
    rx_msg_t msg = {0};
    memcpy(msg.mac, mac, 6);
    msg.hdr = *hdr;
    if (payload && hdr->len) {
        uint8_t n = hdr->len;
        if (n > sizeof(msg.payload)) {
            n = sizeof(msg.payload);
        }
        memcpy(msg.payload, payload, n);
        msg.hdr.len = n;
    }
    /* Keys get a dedicated queue so audio never starves Voice UP. */
    if (hdr->type == VK_PKT_KEY) {
        if (xQueueSend(s_q_key, &msg, 0) != pdTRUE) {
            /* Extremely full: drop one slot then retry (still never drop into audio Q). */
            rx_msg_t dump;
            (void)xQueueReceive(s_q_key, &dump, 0);
            (void)xQueueSend(s_q_key, &msg, 0);
        }
        return;
    }
    (void)xQueueSend(s_q, &msg, 0); /* audio/etc: OK to drop when busy */
}

static void send_pair_ack(const uint8_t mac[6])
{
    rx_radio_set_peer(mac);
    vk_pair_ack_t ack = {0};
    esp_wifi_get_mac(WIFI_IF_STA, ack.mac);
    ack.ver = VK_PROTO_VER;
    ack.unix_time = vk_usb_unix_time();
    rx_radio_send(VK_PKT_PAIR_ACK, &ack, sizeof(ack));
    vk_usb_push_backlight();
    vk_usb_cdc_printf("paired %02X:%02X:%02X:%02X:%02X:%02X time=%lu\r\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                      (unsigned long)ack.unix_time);
}

static void handle_pair_req(const uint8_t mac[6], const uint8_t *payload, uint8_t len)
{
    (void)payload;
    (void)len;
    if (!mac) {
        return;
    }
    /* Already bound to this handheld → refresh ACK (clock / link recover). */
    if (rx_radio_is_peer(mac)) {
        send_pair_ack(mac);
        return;
    }
    /* Bound to someone else → only accept during boot pairing window / after clear. */
    if (rx_radio_has_peer() && !rx_radio_pairing_open()) {
        ESP_LOGW(TAG, "ignore PAIR_REQ from " MACSTR " (bound)", MAC2STR(mac));
        vk_usb_cdc_printf("pair ignore (bound to another)\r\n");
        return;
    }
    if (rx_radio_has_peer()) {
        ESP_LOGI(TAG, "re-pair during window ← " MACSTR, MAC2STR(mac));
    }
    send_pair_ack(mac);
}

static void handle_key(const uint8_t *payload, uint8_t len)
{
    if (!payload || len < sizeof(vk_key_t)) {
        return;
    }
    const vk_key_t *k = (const vk_key_t *)payload;
    /* Always mirror to CDC so the config page can animate, even if HID ignores UP. */
    vk_usb_cdc_printf("key %u %s\r\n", k->key, k->action == VK_ACT_DOWN ? "down" : "up");

    if (k->key == VK_KEY_VOICE) {
        if (k->action == VK_ACT_DOWN) {
            /* Re-press while stuck: clear host modifiers first. */
            if (s_voice_held) {
                vk_hid_report_t clear = {0};
                vk_usb_send_report(&clear);
            }
            s_voice_held = true;
            s_voice_held_ms = now_ms();
            s_last_voice_audio_ms = s_voice_held_ms;
            audio_ring_set_live(true);
        } else {
            s_voice_held = false;
            audio_ring_set_live(false);
        }
    }

    vk_hid_report_t report;
    if (!vk_hid_from_key((vk_key_id_t)k->key, (vk_act_t)k->action, &report)) {
        return;
    }
    vk_usb_send_report(&report);
}

static void handle_audio(const uint8_t *payload, uint8_t len)
{
    if (!payload || len < 2 || !audio_ring_is_live()) {
        return;
    }
    if (s_voice_held) {
        s_last_voice_audio_ms = now_ms();
    }
    size_t samples = len / sizeof(int16_t);
    audio_ring_write((const int16_t *)payload, samples);
}

static void handle_hh_telemetry(const uint8_t *payload, uint8_t len)
{
    if (!payload || len < 2) {
        return;
    }
    /* STATUS is 2 bytes; HEARTBEAT starts with battery+flags too. */
    vk_usb_hh_update(payload[0], payload[1]);
}

static void handle_msg(const rx_msg_t *msg)
{
    switch (msg->hdr.type) {
    case VK_PKT_PAIR_REQ:
        handle_pair_req(msg->mac, msg->payload, msg->hdr.len);
        break;
    case VK_PKT_KEY:
        /* Drop keys from non-bound handhelds so two sets don't cross-talk. */
        if (rx_radio_has_peer() && !rx_radio_is_peer(msg->mac)) {
            break;
        }
        handle_key(msg->payload, msg->hdr.len);
        break;
    case VK_PKT_AUDIO:
        if (rx_radio_has_peer() && !rx_radio_is_peer(msg->mac)) {
            break;
        }
        handle_audio(msg->payload, msg->hdr.len);
        break;
    case VK_PKT_HEARTBEAT:
    case VK_PKT_STATUS:
        if (rx_radio_has_peer() && !rx_radio_is_peer(msg->mac)) {
            break;
        }
        handle_hh_telemetry(msg->payload, msg->hdr.len);
        if (!rx_radio_has_peer()) {
            send_pair_ack(msg->mac);
        }
        break;
    default:
        break;
    }
}

static void app_task(void *arg)
{
    (void)arg;
    uint32_t last_hb = 0;
    rx_msg_t msg;

    while (true) {
        /* Prefer keys: drain key queue fully before one general packet. */
        while (xQueueReceive(s_q_key, &msg, 0) == pdTRUE) {
            handle_msg(&msg);
        }
        if (xQueueReceive(s_q, &msg, pdMS_TO_TICKS(20)) == pdTRUE) {
            handle_msg(&msg);
            while (xQueueReceive(s_q_key, &msg, 0) == pdTRUE) {
                handle_msg(&msg);
            }
        }

        uint32_t t = now_ms();
        rx_radio_tick(t);
        vk_usb_hh_tick(t);

        /*
         * Voice UP lost while talking: audio stops shortly after handheld releases,
         * but HID modifiers stay down. If we were live and silence lasts, force UP.
         */
        if (s_voice_held) {
            uint32_t silence = t - s_last_voice_audio_ms;
            uint32_t held = t - s_voice_held_ms;
            if ((silence > 400 && held > 200) || held > 120000) {
                force_keys_up();
            }
        }

        if (rx_radio_has_peer() && (t - last_hb) > VK_HB_MS) {
            last_hb = t;
            vk_heartbeat_t hb = {
                .battery = 0,
                .flags = 0,
                .unix_time = vk_usb_unix_time(),
            };
            rx_radio_send(VK_PKT_HEARTBEAT, &hb, sizeof(hb));
        }
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    s_q = xQueueCreate(24, sizeof(rx_msg_t));
    s_q_key = xQueueCreate(16, sizeof(rx_msg_t));
    vk_usb_init();
    rx_radio_init(on_radio, NULL);

    xTaskCreate(app_task, "app", 6144, NULL, 5, NULL);
    ESP_LOGI(TAG, "receiver ready — Zhiyan Receiver");
    vk_usb_cdc_printf("Zhiyan Receiver ready. T?/T; S?; K?/K; B?/B; I?/I; P?/P!; O/#RGB WORD.\r\n");
}
