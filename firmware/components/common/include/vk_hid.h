#pragma once

#include "vk_protocol.h"

#include <stdbool.h>
#include <stdint.h>

#define VK_HID_ENTER      0x28
#define VK_HID_BACKSPACE  0x2A
#define VK_HID_MOD_LCTRL  0x01
#define VK_HID_MOD_LSHIFT 0x02
#define VK_HID_MOD_LALT   0x04
#define VK_HID_MOD_LGUI   0x08
#define VK_HID_MOD_RCTRL  0x10
#define VK_HID_MOD_RSHIFT 0x20
#define VK_HID_MOD_RALT   0x40
#define VK_HID_MOD_RGUI   0x80

typedef struct {
    uint8_t modifiers;
    uint8_t keycode;
    uint8_t pulse;
} vk_hid_report_t;

typedef struct {
    uint8_t modifiers;
    uint8_t keycode;
} vk_hid_binding_t;

typedef struct {
    vk_hid_binding_t voice;
    vk_hid_binding_t yes;
    vk_hid_binding_t no;
} vk_hid_map_t;

void vk_hid_map_default(vk_hid_map_t *map);
const vk_hid_map_t *vk_hid_map_get(void);
void vk_hid_map_set(const vk_hid_map_t *map);

bool vk_hid_from_key(vk_key_id_t key, vk_act_t action, vk_hid_report_t *out);
