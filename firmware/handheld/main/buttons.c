#include "buttons.h"

#include "board.h"
#include "vk_keys.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void buttons_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_BTN_VOICE) | (1ULL << PIN_BTN_YES) | (1ULL << PIN_BTN_NO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

uint8_t buttons_raw_mask(void)
{
    uint8_t m = 0;
    /* Active low */
    if (!gpio_get_level(PIN_BTN_VOICE)) {
        m |= VK_KEYMASK_VOICE;
    }
    if (!gpio_get_level(PIN_BTN_YES)) {
        m |= VK_KEYMASK_YES;
    }
    if (!gpio_get_level(PIN_BTN_NO)) {
        m |= VK_KEYMASK_NO;
    }
    return m;
}

bool buttons_boot_hold(uint8_t mask, uint32_t hold_ms)
{
    uint32_t held = 0;
    uint32_t no_press = 0;
    uint32_t window = hold_ms + 300;
    uint32_t t0 = (uint32_t)(esp_timer_get_time() / 1000ULL);
    while (((uint32_t)(esp_timer_get_time() / 1000ULL) - t0) < window) {
        if (buttons_raw_mask() & mask) {
            no_press = 0;
            held += 20;
            if (held >= hold_ms) {
                return true;
            }
        } else if (held == 0) {
            no_press += 20;
            if (no_press >= 250) {
                return false;
            }
        } else {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}
