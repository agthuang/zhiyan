#include "battery_hw.h"
#include "ble_media.h"
#include "buttons.h"
#include "lcd.h"
#include "mic.h"
#include "radio.h"

#include "vk_keys.h"
#include "vk_protocol.h"
#include "vk_ui.h"
#include "vk_idle.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "app";

typedef enum {
    APP_PAIRING = 0,
    APP_LINKED,
    APP_AWAY,
} app_link_t;

static vk_keys_t s_keys;
static app_link_t s_link = APP_PAIRING;
static uint32_t s_last_rx_ms;
static uint32_t s_unix;
static bool s_time_ok;
static int16_t s_tz = VK_TZ_DEFAULT_MIN;
static battery_sample_t s_bat;
static vk_ui_mode_t s_hold = VK_UI_IDLE; /* YES/NO while key held */
static bool s_talking;
static bool s_osd_on;
static uint16_t s_osd_color;
static char s_osd_text[VK_OSD_TEXT_MAX + 1];
static vk_idle_t s_idle;
static bool s_eco_radio; /* NVS / web: ESP-NOW connectionless RF eco */

/* Eco idle heartbeat: stay under receiver VK_HB_TIMEOUT_MS (1600). */
#define VK_HB_MS_ECO       900
#define VK_HB_TIMEOUT_ECO  2800
#define VK_ECO_IDLE_BLANK_MS 10000

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void time_nvs_save(uint32_t unix_time)
{
    nvs_handle_t h;
    if (nvs_open("vk", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u32(h, "unix", unix_time);
    nvs_commit(h);
    nvs_close(h);
}

static void apply_unix(uint32_t unix_time)
{
    if (!vk_unix_is_valid(unix_time)) {
        return;
    }
    bool first = !s_time_ok;
    time_t cur = time(NULL);
    bool big_jump = (cur > 0) &&
                    (llabs((long long)unix_time - (long long)cur) > 120LL);
    s_unix = unix_time;
    s_time_ok = true;
    struct timeval tv = {
        .tv_sec = (time_t)unix_time,
        .tv_usec = 0,
    };
    settimeofday(&tv, NULL);
    /* Persist on first sync, minute change, or large correction after power loss. */
    static uint32_t s_saved_min;
    uint32_t min = unix_time / 60u;
    if (first || big_jump || min != s_saved_min) {
        s_saved_min = min;
        time_nvs_save(unix_time);
        if (first || big_jump) {
            ESP_LOGI(TAG, "time sync %lu", (unsigned long)unix_time);
        }
    }
}

static void bl_nvs_save(uint8_t duty)
{
    nvs_handle_t h;
    if (nvs_open("vk", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u8(h, "bl", duty);
    nvs_commit(h);
    nvs_close(h);
}

static uint8_t bl_nvs_load(void)
{
    nvs_handle_t h;
    uint8_t duty = VK_BL_DEFAULT;
    if (nvs_open("vk", NVS_READONLY, &h) != ESP_OK) {
        return duty;
    }
    nvs_get_u8(h, "bl", &duty);
    nvs_close(h);
    return duty;
}

static void idle_nvs_save(bool enabled)
{
    nvs_handle_t h;
    if (nvs_open("vk", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u8(h, "idle", enabled ? 1u : 0u);
    nvs_commit(h);
    nvs_close(h);
}

static bool idle_nvs_load(void)
{
    nvs_handle_t h;
    uint8_t v = 1; /* default: idle blank on (save backlight) */
    if (nvs_open("vk", NVS_READONLY, &h) != ESP_OK) {
        return true;
    }
    if (nvs_get_u8(h, "idle", &v) != ESP_OK) {
        nvs_close(h);
        return true;
    }
    nvs_close(h);
    return v != 0;
}

static void eco_nvs_save(bool enabled)
{
    nvs_handle_t h;
    if (nvs_open("vk", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u8(h, "eco", enabled ? 1u : 0u);
    nvs_commit(h);
    nvs_close(h);
}

static bool eco_nvs_load(void)
{
    nvs_handle_t h;
    uint8_t v = 0; /* default: RF always on */
    if (nvs_open("vk", NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    if (nvs_get_u8(h, "eco", &v) != ESP_OK) {
        nvs_close(h);
        return false;
    }
    nvs_close(h);
    return v != 0;
}

static void on_user_activity(void)
{
    bool was = vk_idle_is_blanked(&s_idle);
    vk_idle_activity(&s_idle, now_ms());
    if (was) {
        lcd_set_backlight(bl_nvs_load());
    }
}

static void apply_backlight(uint8_t duty)
{
    bl_nvs_save(duty);
    on_user_activity();
    if (!vk_idle_is_blanked(&s_idle)) {
        lcd_set_backlight(duty);
    }
    ESP_LOGI(TAG, "backlight %u", (unsigned)duty);
}

static void apply_idle_blank(bool enabled)
{
    idle_nvs_save(enabled);
    vk_idle_set_enabled(&s_idle, enabled, now_ms());
    if (!enabled || !vk_idle_is_blanked(&s_idle)) {
        lcd_set_backlight(bl_nvs_load());
    }
    ESP_LOGI(TAG, "idle blank %s", enabled ? "on" : "off");
}

static void apply_eco_radio(bool enabled)
{
    eco_nvs_save(enabled);
    s_eco_radio = enabled;
    radio_set_modem_sleep(enabled);
    if (enabled) {
        /* Force blank on for battery; leave user's idle NVS preference alone. */
        vk_idle_set_enabled(&s_idle, true, now_ms());
        s_idle.timeout_ms = VK_ECO_IDLE_BLANK_MS;
        ESP_LOGI(TAG, "eco radio on");
    } else {
        bool idle_on = idle_nvs_load();
        vk_idle_set_enabled(&s_idle, idle_on, now_ms());
        s_idle.timeout_ms = VK_IDLE_BLANK_MS_DEFAULT;
        if (!idle_on || !vk_idle_is_blanked(&s_idle)) {
            lcd_set_backlight(bl_nvs_load());
        }
        ESP_LOGI(TAG, "eco radio off");
    }
}

static void send_key(vk_key_id_t key, vk_act_t act)
{
    vk_key_t payload = {
        .key = (uint8_t)key,
        .action = (uint8_t)act,
    };
    /* UP must reach the host or modifiers stick until USB unplug. */
    int tries = (act == VK_ACT_UP) ? 8 : 3;
    for (int i = 0; i < tries; i++) {
        if (radio_send(VK_PKT_KEY, &payload, sizeof(payload))) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(4));
    }
    ESP_LOGW(TAG, "key %u %s TX failed", (unsigned)key, act == VK_ACT_DOWN ? "down" : "up");
}

static void on_mic_frame(const int16_t *samples, size_t count, void *ctx)
{
    (void)ctx;
    radio_send_audio(samples, count);
}

static void on_radio(const uint8_t mac[6], const vk_hdr_t *hdr, const uint8_t *payload, void *ctx)
{
    (void)ctx;
    s_last_rx_ms = now_ms();

    if (hdr->type == VK_PKT_PAIR_ACK && payload && hdr->len >= sizeof(vk_pair_ack_t)) {
        const vk_pair_ack_t *ack = (const vk_pair_ack_t *)payload;
        const uint8_t *peer = ack->mac;
        if (!(peer[0] | peer[1] | peer[2] | peer[3] | peer[4] | peer[5])) {
            peer = mac;
        }
        radio_set_peer(peer);
        apply_unix(ack->unix_time);
        s_link = APP_LINKED;
        ESP_LOGI(TAG, "pair ack");
        return;
    }

    if (hdr->type == VK_PKT_HEARTBEAT && payload && hdr->len >= sizeof(vk_heartbeat_t)) {
        const vk_heartbeat_t *hb = (const vk_heartbeat_t *)payload;
        apply_unix(hb->unix_time);
        if (s_link != APP_LINKED) {
            s_link = APP_LINKED;
        }
        return;
    }

    if (hdr->type == VK_PKT_BACKLIGHT && payload && hdr->len >= sizeof(vk_backlight_t)) {
        const vk_backlight_t *bl = (const vk_backlight_t *)payload;
        apply_backlight(bl->duty);
        return;
    }

    if (hdr->type == VK_PKT_IDLE_BLANK && payload && hdr->len >= sizeof(vk_idle_blank_t)) {
        const vk_idle_blank_t *cfg = (const vk_idle_blank_t *)payload;
        apply_idle_blank(cfg->enabled != 0);
        if (cfg->timeout_sec != 0) {
            s_idle.timeout_ms = (uint32_t)cfg->timeout_sec * 1000u;
        } else if (s_eco_radio) {
            s_idle.timeout_ms = VK_ECO_IDLE_BLANK_MS;
        }
        return;
    }

    if (hdr->type == VK_PKT_ECO && payload && hdr->len >= sizeof(vk_eco_t)) {
        const vk_eco_t *cfg = (const vk_eco_t *)payload;
        apply_eco_radio(cfg->enabled != 0);
        return;
    }

    if (hdr->type == VK_PKT_OSD && payload && hdr->len >= sizeof(vk_osd_t)) {
        const vk_osd_t *osd = (const vk_osd_t *)payload;
        on_user_activity();
        if (osd->flags & VK_OSD_FLAG_CLEAR) {
            s_osd_on = false;
            s_osd_text[0] = 0;
            ESP_LOGI(TAG, "osd clear");
            return;
        }
        uint8_t n = osd->len;
        if (n > VK_OSD_TEXT_MAX) {
            n = VK_OSD_TEXT_MAX;
        }
        char tmp[VK_OSD_TEXT_MAX + 1];
        memcpy(tmp, osd->text, n);
        tmp[n] = 0;
        s_osd_color = osd->color;
        s_osd_on = vk_osd_sanitize(s_osd_text, sizeof(s_osd_text), tmp) > 0;
        ESP_LOGI(TAG, "osd %s", s_osd_on ? s_osd_text : "(empty)");
    }
}

static vk_ui_model_t make_ui(void)
{
    vk_ui_model_t m = {0};
    m.battery = s_bat.percent;
    m.chg_full = s_bat.done && !s_bat.charging;
    m.charging = s_bat.charging && !m.chg_full;
    /* Only pulse while charging — otherwise dirty-check flushes LCD ~2.5 Hz forever. */
    m.blink = m.charging ? (uint8_t)((now_ms() / 400) & 1u) : 0;
    m.linked = (s_link == APP_LINKED);
    m.time_valid = s_time_ok;

    time_t now = time(NULL);
    if (s_time_ok && now > 0) {
        uint8_t h, mi;
        vk_unix_to_hm((uint32_t)now, s_tz, &h, &mi);
        m.hour = h;
        m.minute = mi;
    } else if (s_time_ok) {
        vk_unix_to_hm(s_unix, s_tz, &m.hour, &m.minute);
    }

    if (s_talking) {
        m.mode = VK_UI_TALK;
    } else if (s_hold == VK_UI_YES || s_hold == VK_UI_NO) {
        m.mode = s_hold;
    } else if (s_link == APP_PAIRING) {
        m.mode = VK_UI_PAIR;
    } else if (s_link == APP_AWAY) {
        m.mode = VK_UI_AWAY;
    } else if (s_osd_on) {
        m.mode = VK_UI_OSD;
        m.osd_color = s_osd_color;
        memcpy(m.osd_text, s_osd_text, sizeof(m.osd_text));
    } else {
        m.mode = VK_UI_IDLE;
    }

    /* Animate only moving screens — ~2 fps so SPI stays quiet. */
    if (m.mode == VK_UI_AWAY || (m.mode == VK_UI_IDLE && !m.time_valid)) {
        m.anim = (uint8_t)((now_ms() / 500) & 0xFFu);
    }
    return m;
}

static void handle_events(uint8_t ev)
{
    if (ev) {
        on_user_activity();
    }
    if (ev & VK_EV_UNPAIR) {
        ESP_LOGW(TAG, "unpair");
        radio_clear_peer();
        s_link = APP_PAIRING;
        s_talking = false;
        s_hold = VK_UI_IDLE;
        s_osd_on = false;
        mic_set_streaming(false);
        return;
    }
    if (ev & VK_EV_VOICE_DOWN) {
        s_talking = true;
        radio_modem_boost();
        mic_set_streaming(true);
        send_key(VK_KEY_VOICE, VK_ACT_DOWN);
    }
    if (ev & VK_EV_VOICE_UP) {
        s_talking = false;
        mic_set_streaming(false);
        /* Let in-flight audio finish so KEY UP is not starved on the radio. */
        for (int i = 0; i < 15 && !radio_tx_ready(); i++) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        vTaskDelay(pdMS_TO_TICKS(8));
        /* Repeat: ESP-NOW can drop the only UP while audio is still on the air.
         * Host keeps Right-Ctrl/Right-Cmd down until an all-zero HID report lands. */
        for (int n = 0; n < 3; n++) {
            send_key(VK_KEY_VOICE, VK_ACT_UP);
            if (n + 1 < 3) {
                vTaskDelay(pdMS_TO_TICKS(6));
            }
        }
        radio_modem_unboost();
    }
    if (ev & VK_EV_YES_DOWN) {
        send_key(VK_KEY_YES, VK_ACT_DOWN);
        s_hold = VK_UI_YES;
    }
    if (ev & VK_EV_YES_UP) {
        send_key(VK_KEY_YES, VK_ACT_UP);
        if (s_hold == VK_UI_YES) {
            s_hold = VK_UI_IDLE;
        }
    }
    if (ev & VK_EV_NO_DOWN) {
        send_key(VK_KEY_NO, VK_ACT_DOWN);
        s_hold = VK_UI_NO;
    }
    if (ev & VK_EV_NO_UP) {
        send_key(VK_KEY_NO, VK_ACT_UP);
        if (s_hold == VK_UI_NO) {
            s_hold = VK_UI_IDLE;
        }
    }
}

static void app_task(void *arg)
{
    (void)arg;
    uint32_t last_pair = 0;
    uint32_t last_hb = 0;
    uint32_t last_bat = 0;

    while (true) {
        uint32_t t = now_ms();
        uint8_t ev = vk_keys_feed(&s_keys, buttons_raw_mask(), t);
        if (ev) {
            handle_events(ev);
        }

        /* Keep screen awake while talking / holding yes-no / pairing / OSD. */
        if (s_talking || s_osd_on || s_hold == VK_UI_YES || s_hold == VK_UI_NO ||
            s_link == APP_PAIRING) {
            on_user_activity();
        }

        if (vk_idle_tick(&s_idle, t)) {
            if (vk_idle_is_blanked(&s_idle)) {
                lcd_set_backlight(0);
                ESP_LOGI(TAG, "idle blank");
            } else {
                lcd_set_backlight(bl_nvs_load());
            }
        }

        if (s_link == APP_LINKED && s_last_rx_ms &&
            (t - s_last_rx_ms) > (s_eco_radio ? VK_HB_TIMEOUT_ECO : VK_HB_TIMEOUT_MS)) {
            s_link = APP_AWAY;
            ESP_LOGW(TAG, "link timeout");
        }

        /* Keep probing until linked — avoids AWAY+saved-peer deadlock. */
        uint32_t pair_gap = s_eco_radio ? 1500u : 800u;
        if (s_link != APP_LINKED && (t - last_pair) > pair_gap) {
            last_pair = t;
            radio_send_pair_req();
            if (!radio_has_peer()) {
                s_link = APP_PAIRING;
            }
        }

        /* Heartbeat even while talking, so the receiver can see Voice release
         * (no VK_STAT_TALKING) if the KEY UP packet was lost. */
        uint32_t hb_ms = VK_HB_MS;
        if (s_eco_radio && !s_talking) {
            hb_ms = VK_HB_MS_ECO;
        }
        if (radio_has_peer() && (t - last_hb) > hb_ms) {
            last_hb = t;
            vk_heartbeat_t hb = {
                .battery = s_bat.percent,
                .flags = 0,
                .unix_time = 0,
            };
            if (s_bat.charging) {
                hb.flags |= VK_STAT_CHARGING;
            }
            if (s_bat.done) {
                hb.flags |= VK_STAT_CHG_DONE;
            }
            if (s_talking) {
                hb.flags |= VK_STAT_TALKING;
            }
            radio_send(VK_PKT_HEARTBEAT, &hb, sizeof(hb));
        }

        if ((t - last_bat) > 2000) {
            last_bat = t;
            s_bat = battery_hw_read();
        }

        /* Backlight off: skip SPI redraw (panel still holds last frame). */
        if (!vk_idle_is_blanked(&s_idle)) {
            vk_ui_model_t ui = make_ui();
            lcd_show(&ui);
        }
        vTaskDelay(pdMS_TO_TICKS(vk_idle_is_blanked(&s_idle) ? 100 : 20));
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

    vk_keys_init(&s_keys);
    buttons_init();
    battery_hw_init();
    s_bat = battery_hw_read();
    lcd_init();
    lcd_set_backlight(bl_nvs_load());
    vk_idle_init(&s_idle, idle_nvs_load(), now_ms());
    if (s_idle.enabled) {
        ESP_LOGI(TAG, "idle blank enabled (%u ms)", (unsigned)s_idle.timeout_ms);
    }

    /*
     * Boot gesture (key already held while flipping power):
     *   Yes ≥1.5s → BLE media
     * RF eco is web/NVS only (CDC E); no boot-hold No.
     */
    if (ble_media_boot_hold_yes(1500)) {
        ESP_LOGI(TAG, "boot: BLE media mode (Yes held at power-on)");
        ble_media_run(); /* never returns */
    }

    radio_init(on_radio, NULL);
    if (eco_nvs_load()) {
        apply_eco_radio(true);
    }
    mic_init(on_mic_frame, NULL);

    /* Do not restore NVS as a wall clock — it freezes at last unplug.
     * Idle shows the wait-pet until the receiver pushes a host T. */

    if (radio_has_peer()) {
        s_link = APP_AWAY; /* wait for receiver heartbeat / re-pair */
        s_last_rx_ms = 0;
    }

    vk_ui_model_t boot_ui = {
        .mode = VK_UI_OSD,
        .battery = s_bat.percent,
        .charging = s_bat.charging && !s_bat.done,
        .chg_full = s_bat.done && !s_bat.charging,
        .linked = 0,
        .time_valid = 0,
        .anim = 5,
        .osd_color = s_eco_radio ? vk_rgb888_to_565(0x60, 0xC4, 0x84)
                                 : vk_rgb888_to_565(0xEC, 0xB0, 0x48),
    };
    if (s_eco_radio) {
        snprintf(boot_ui.osd_text, sizeof(boot_ui.osd_text), "ECO");
    } else if (radio_has_peer()) {
        boot_ui.mode = VK_UI_AWAY;
        boot_ui.osd_text[0] = 0;
    } else {
        boot_ui.mode = VK_UI_PAIR;
        boot_ui.osd_text[0] = 0;
    }
    lcd_show(&boot_ui);

    xTaskCreate(app_task, "app", 6144, NULL, 5, NULL);
    ESP_LOGI(TAG, "handheld ready%s", s_eco_radio ? " (eco)" : "");
}
