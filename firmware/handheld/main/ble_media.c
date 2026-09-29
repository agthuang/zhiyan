#include "ble_media.h"

#include "battery_hw.h"
#include "buttons.h"
#include "lcd.h"

#include "vk_keys.h"
#include "vk_ui.h"

#include "esp_hid_gap.h"
#include "esp_hidd.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#if CONFIG_BT_BLE_ENABLED
#include "esp_gatts_api.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "ble_media";

#define HID_RPT_ID_KB 1
#define HID_KB_RPT_LEN 8

#define HID_KEY_SPACE      0x2C
#define HID_KEY_ARROW_DOWN 0x51
#define HID_KEY_ARROW_UP   0x52

static esp_hidd_dev_t *s_hid;
static bool s_connected;
static battery_sample_t s_bat;
static vk_ui_mode_t s_hold = VK_UI_IDLE;

static const uint8_t s_kb_map[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, HID_RPT_ID_KB,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08,
    0x81, 0x01, 0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01,
    0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07,
    0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0,
};

static esp_hid_raw_report_map_t s_report_maps[] = {
    {.data = s_kb_map, .len = sizeof(s_kb_map)},
};

static esp_hid_device_config_t s_hid_cfg = {
    .vendor_id = 0x303A,
    .product_id = 0x1009,
    .version = 0x0105,
    .device_name = "知言",
    .manufacturer_name = "Zhiyan",
    .serial_number = "VK-BT-06",
    .report_maps = s_report_maps,
    .report_maps_len = 1,
};

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

bool ble_media_boot_hold_yes(uint32_t hold_ms)
{
    return buttons_boot_hold(VK_KEYMASK_YES, hold_ms);
}

static void kb_send(uint8_t keycode)
{
    if (!s_hid || !s_connected) {
        return;
    }
    uint8_t buf[HID_KB_RPT_LEN] = {0};
    buf[2] = keycode;
    esp_err_t err = esp_hidd_dev_input_set(s_hid, 0, HID_RPT_ID_KB, buf, HID_KB_RPT_LEN);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "kb report: %s", esp_err_to_name(err));
    }
}

static void kb_release(void)
{
    if (!s_hid || !s_connected) {
        return;
    }
    uint8_t buf[HID_KB_RPT_LEN] = {0};
    esp_hidd_dev_input_set(s_hid, 0, HID_RPT_ID_KB, buf, HID_KB_RPT_LEN);
}

static void on_hidd(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;

    switch (event) {
    case ESP_HIDD_START_EVENT:
        ESP_LOGI(TAG, "HID start — advertising as 知言 (keyboard)");
        {
            esp_err_t adv = esp_hid_ble_gap_adv_start();
            if (adv != ESP_OK) {
                ESP_LOGE(TAG, "adv_start failed: %s", esp_err_to_name(adv));
            }
        }
        break;
    case ESP_HIDD_CONNECT_EVENT:
        s_connected = true;
        ESP_LOGI(TAG, "phone connected");
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        s_connected = false;
        kb_release();
        ESP_LOGI(TAG, "phone disconnected — re-advertise");
        esp_hid_ble_gap_adv_start();
        break;
    default:
        (void)param;
        break;
    }
}

static vk_ui_model_t make_ui(void)
{
    vk_ui_model_t m = {0};
    m.battery = s_bat.percent;
    m.chg_full = s_bat.done && !s_bat.charging;
    m.charging = s_bat.charging && !m.chg_full;
    m.blink = (uint8_t)((now_ms() / 400) & 1u);
    m.linked = s_connected ? 1 : 0;
    m.time_valid = 0;
    if (s_hold == VK_UI_YES || s_hold == VK_UI_NO || s_hold == VK_UI_TALK) {
        m.mode = s_hold;
    } else {
        m.mode = VK_UI_MEDIA;
    }
    return m;
}

static void handle_events(uint8_t ev)
{
    if (ev & VK_EV_VOICE_DOWN) {
        s_hold = VK_UI_TALK;
        kb_send(HID_KEY_ARROW_UP);
    }
    if (ev & VK_EV_VOICE_UP) {
        kb_release();
        if (s_hold == VK_UI_TALK) {
            s_hold = VK_UI_IDLE;
        }
    }
    if (ev & VK_EV_YES_DOWN) {
        s_hold = VK_UI_YES;
        kb_send(HID_KEY_SPACE);
    }
    if (ev & VK_EV_YES_UP) {
        kb_release();
        if (s_hold == VK_UI_YES) {
            s_hold = VK_UI_IDLE;
        }
    }
    if (ev & VK_EV_NO_DOWN) {
        s_hold = VK_UI_NO;
        kb_send(HID_KEY_ARROW_DOWN);
    }
    if (ev & VK_EV_NO_UP) {
        kb_release();
        if (s_hold == VK_UI_NO) {
            s_hold = VK_UI_IDLE;
        }
    }
}

void ble_media_run(void)
{
    vk_keys_t keys;
    vk_keys_init(&keys);
    s_bat = battery_hw_read();

    vk_ui_model_t boot = {
        .mode = VK_UI_MEDIA,
        .battery = s_bat.percent,
        .charging = s_bat.charging && !s_bat.done,
        .chg_full = s_bat.done && !s_bat.charging,
        .linked = 0,
    };
    lcd_show(&boot);

    ESP_ERROR_CHECK(esp_hid_gap_init(HIDD_BLE_MODE));
    ESP_ERROR_CHECK(esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_KEYBOARD, s_hid_cfg.device_name));
#if CONFIG_BT_BLE_ENABLED
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(esp_hidd_gatts_event_handler));
#endif
    ESP_ERROR_CHECK(esp_hidd_dev_init(&s_hid_cfg, ESP_HID_TRANSPORT_BLE, on_hidd, &s_hid));

    ESP_LOGI(TAG, "知言 BLE keyboard — Up / Space / Down");

    uint32_t last_bat = 0;
    while (true) {
        uint32_t t = now_ms();
        uint8_t ev = vk_keys_feed(&keys, buttons_raw_mask(), t);
        if (ev) {
            handle_events(ev);
        }
        if ((t - last_bat) > 2000) {
            last_bat = t;
            s_bat = battery_hw_read();
        }
        vk_ui_model_t ui = make_ui();
        lcd_show(&ui);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void ble_hid_task_start_up(void)
{
}
