#pragma once

#include <stdint.h>

#define VK_KEYMASK_VOICE  (1u << 0)
#define VK_KEYMASK_YES    (1u << 1)
#define VK_KEYMASK_NO     (1u << 2)

#define VK_EV_VOICE_DOWN  (1u << 0)
#define VK_EV_VOICE_UP    (1u << 1)
#define VK_EV_YES_DOWN    (1u << 2)
#define VK_EV_YES_UP      (1u << 3)
#define VK_EV_NO_DOWN     (1u << 4)
#define VK_EV_NO_UP       (1u << 5)
#define VK_EV_UNPAIR      (1u << 6)

typedef struct {
    uint8_t  stable;
    uint8_t  last_raw;
    uint8_t  same_count;
    uint8_t  unpair_fired;
    uint32_t combo_start_ms;
} vk_keys_t;

void vk_keys_init(vk_keys_t *k);
uint8_t vk_keys_feed(vk_keys_t *k, uint8_t raw_pressed, uint32_t now_ms);
