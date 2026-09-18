#include "vk_hid.h"

static vk_hid_map_t s_map = {
    .voice = {.modifiers = (uint8_t)(VK_HID_MOD_RCTRL | VK_HID_MOD_RGUI), .keycode = 0},
    .yes = {.modifiers = 0, .keycode = VK_HID_ENTER},
    .no = {.modifiers = 0, .keycode = VK_HID_BACKSPACE},
};

void vk_hid_map_default(vk_hid_map_t *map)
{
    if (!map) {
        return;
    }
    map->voice.modifiers = (uint8_t)(VK_HID_MOD_RCTRL | VK_HID_MOD_RGUI);
    map->voice.keycode = 0;
    map->yes.modifiers = 0;
    map->yes.keycode = VK_HID_ENTER;
    map->no.modifiers = 0;
    map->no.keycode = VK_HID_BACKSPACE;
}

const vk_hid_map_t *vk_hid_map_get(void)
{
    return &s_map;
}

void vk_hid_map_set(const vk_hid_map_t *map)
{
    if (!map) {
        return;
    }
    s_map = *map;
}

bool vk_hid_from_key(vk_key_id_t key, vk_act_t action, vk_hid_report_t *out)
{
    if (out == NULL) {
        return false;
    }
    out->modifiers = 0;
    out->keycode = 0;
    out->pulse = 0;

    const vk_hid_binding_t *b = NULL;
    int pulse = 0;
    switch (key) {
    case VK_KEY_VOICE:
        b = &s_map.voice;
        pulse = 0;
        break;
    case VK_KEY_YES:
        b = &s_map.yes;
        pulse = 0; /* hold: OS autorepeat while pressed */
        break;
    case VK_KEY_NO:
        b = &s_map.no;
        pulse = 0; /* hold: OS autorepeat deletes while pressed */
        break;
    default:
        return false;
    }

    if (action == VK_ACT_UP) {
        /* Release clears the report (voice / yes / no all hold-style). */
        return true;
    }

    /* VK_ACT_DOWN */
    out->modifiers = b->modifiers;
    out->keycode = b->keycode;
    out->pulse = (uint8_t)pulse;
    return true;
}
