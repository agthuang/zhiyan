#include "status_led.h"

#include "board.h"

#include "esp_log.h"
#include "led_strip.h"

static const char *TAG = "status_led";

/* Keep brightness low — stick sits next to the screen/desk. */
enum {
    BR_UNLINK_ON = 12,
    BR_LINK = 18,
    BR_VOICE = 28,
    BLINK_MS = 450,
};

typedef enum {
    LED_MODE_UNLINKED = 0,
    LED_MODE_LINKED,
    LED_MODE_VOICE,
} led_mode_t;

static led_strip_handle_t s_strip;
static led_mode_t s_mode = LED_MODE_UNLINKED;
static bool s_blink_on;
static uint32_t s_last_blink_ms;
static uint8_t s_last_r, s_last_g, s_last_b;
static bool s_have_color;

static void apply_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip) {
        return;
    }
    if (s_have_color && s_last_r == r && s_last_g == g && s_last_b == b) {
        return;
    }
    esp_err_t err = led_strip_set_pixel(s_strip, 0, r, g, b);
    if (err != ESP_OK) {
        return;
    }
    err = led_strip_refresh(s_strip);
    if (err != ESP_OK) {
        return;
    }
    s_last_r = r;
    s_last_g = g;
    s_last_b = b;
    s_have_color = true;
}

void status_led_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = PIN_STATUS_LED,
        .max_leds = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags = { .invert_out = false },
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = { .with_dma = false },
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WS2812 init failed: %s (no status LED)", esp_err_to_name(err));
        s_strip = NULL;
        return;
    }
    led_strip_clear(s_strip);
    ESP_LOGI(TAG, "WS2812 on GPIO%d", (int)PIN_STATUS_LED);
}

void status_led_tick(uint32_t now_ms, bool linked, bool voice_held)
{
    if (!s_strip) {
        return;
    }

    led_mode_t mode = LED_MODE_UNLINKED;
    if (voice_held && linked) {
        mode = LED_MODE_VOICE;
    } else if (linked) {
        mode = LED_MODE_LINKED;
    }

    if (mode != s_mode) {
        s_mode = mode;
        s_blink_on = true;
        s_last_blink_ms = now_ms;
        s_have_color = false; /* force refresh */
    }

    switch (s_mode) {
    case LED_MODE_VOICE:
        /* Warm amber while PTT is down. */
        apply_rgb(BR_VOICE, BR_VOICE / 3, 0);
        break;
    case LED_MODE_LINKED:
        apply_rgb(0, BR_LINK, 0);
        break;
    case LED_MODE_UNLINKED:
    default:
        if ((now_ms - s_last_blink_ms) >= BLINK_MS) {
            s_last_blink_ms = now_ms;
            s_blink_on = !s_blink_on;
            s_have_color = false;
        }
        if (s_blink_on) {
            apply_rgb(0, 0, BR_UNLINK_ON);
        } else {
            apply_rgb(0, 0, 0);
        }
        break;
    }
}
