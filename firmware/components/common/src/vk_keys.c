#include "vk_keys.h"

#define VK_DEBOUNCE_SAMPLES 3
#define VK_UNPAIR_HOLD_MS   2500

void vk_keys_init(vk_keys_t *k)
{
    k->stable = 0;
    k->last_raw = 0;
    k->same_count = 0;
    k->unpair_fired = 0;
    k->combo_start_ms = 0;
}

uint8_t vk_keys_feed(vk_keys_t *k, uint8_t raw_pressed, uint32_t now_ms)
{
    uint8_t ev = 0;
    raw_pressed &= (uint8_t)(VK_KEYMASK_VOICE | VK_KEYMASK_YES | VK_KEYMASK_NO);

    if (raw_pressed == k->last_raw) {
        if (k->same_count < 255) {
            k->same_count++;
        }
    } else {
        k->last_raw = raw_pressed;
        k->same_count = 1;
    }

    if (k->same_count == VK_DEBOUNCE_SAMPLES) {
        uint8_t changed = (uint8_t)(k->stable ^ raw_pressed);
        uint8_t pressed = (uint8_t)(changed & raw_pressed);
        uint8_t released = (uint8_t)(changed & k->stable);
        k->stable = raw_pressed;

        if (pressed & VK_KEYMASK_VOICE) {
            ev |= VK_EV_VOICE_DOWN;
        }
        if (released & VK_KEYMASK_VOICE) {
            ev |= VK_EV_VOICE_UP;
        }
        if (pressed & VK_KEYMASK_YES) {
            ev |= VK_EV_YES_DOWN;
        }
        if (released & VK_KEYMASK_YES) {
            ev |= VK_EV_YES_UP;
        }
        if (pressed & VK_KEYMASK_NO) {
            ev |= VK_EV_NO_DOWN;
        }
        if (released & VK_KEYMASK_NO) {
            ev |= VK_EV_NO_UP;
        }
    }

    uint8_t combo = (uint8_t)(k->stable & (VK_KEYMASK_YES | VK_KEYMASK_NO));
    if (combo == (VK_KEYMASK_YES | VK_KEYMASK_NO)) {
        if (k->combo_start_ms == 0) {
            k->combo_start_ms = now_ms == 0 ? 1 : now_ms;
        }
        if (!k->unpair_fired && (now_ms - k->combo_start_ms) >= VK_UNPAIR_HOLD_MS) {
            k->unpair_fired = 1;
            ev |= VK_EV_UNPAIR;
        }
    } else {
        k->combo_start_ms = 0;
        k->unpair_fired = 0;
    }

    return ev;
}
