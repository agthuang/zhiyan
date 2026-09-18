#pragma once

#include <stdbool.h>
#include <stdint.h>

#define VK_IDLE_BLANK_MS_DEFAULT 20000u

typedef struct {
    bool enabled;
    bool blanked;
    uint32_t last_activity_ms;
    uint32_t timeout_ms;
} vk_idle_t;

void vk_idle_init(vk_idle_t *s, bool enabled, uint32_t now_ms);
void vk_idle_set_enabled(vk_idle_t *s, bool enabled, uint32_t now_ms);
/** Mark user/host activity: wakes screen and resets idle timer. */
void vk_idle_activity(vk_idle_t *s, uint32_t now_ms);
/**
 * Advance idle timer. Returns true if blanked state changed.
 * When blanked becomes true, caller should turn backlight off;
 * when false after activity, restore saved backlight.
 */
bool vk_idle_tick(vk_idle_t *s, uint32_t now_ms);
bool vk_idle_is_blanked(const vk_idle_t *s);
