#include "vk_idle.h"

void vk_idle_init(vk_idle_t *s, bool enabled, uint32_t now_ms)
{
    if (!s) {
        return;
    }
    s->enabled = enabled;
    s->blanked = false;
    s->last_activity_ms = now_ms;
    s->timeout_ms = VK_IDLE_BLANK_MS_DEFAULT;
}

void vk_idle_set_enabled(vk_idle_t *s, bool enabled, uint32_t now_ms)
{
    if (!s) {
        return;
    }
    s->enabled = enabled;
    s->last_activity_ms = now_ms;
    if (!enabled) {
        s->blanked = false;
    }
}

void vk_idle_activity(vk_idle_t *s, uint32_t now_ms)
{
    if (!s) {
        return;
    }
    s->last_activity_ms = now_ms;
    s->blanked = false;
}

bool vk_idle_tick(vk_idle_t *s, uint32_t now_ms)
{
    if (!s) {
        return false;
    }
    if (!s->enabled) {
        if (s->blanked) {
            s->blanked = false;
            return true;
        }
        return false;
    }
    if (s->blanked) {
        return false;
    }
    uint32_t elapsed = now_ms - s->last_activity_ms;
    if (elapsed >= s->timeout_ms) {
        s->blanked = true;
        return true;
    }
    return false;
}

bool vk_idle_is_blanked(const vk_idle_t *s)
{
    return s && s->blanked;
}
