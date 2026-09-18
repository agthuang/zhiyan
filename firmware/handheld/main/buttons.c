#include "buttons.h"

#include "board.h"
#include "vk_keys.h"

#include "driver/gpio.h"

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
