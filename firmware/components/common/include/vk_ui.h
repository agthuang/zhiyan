#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define VK_LCD_W 160
#define VK_LCD_H 80

typedef enum {
    VK_UI_PAIR = 0,
    VK_UI_IDLE,
    VK_UI_TALK,
    VK_UI_YES,
    VK_UI_NO,
    VK_UI_AWAY,
    VK_UI_MEDIA, /* BLE Douyin / short-video remote */
    VK_UI_OSD,   /* host-pushed colored English word */
} vk_ui_mode_t;

typedef struct {
    vk_ui_mode_t mode;
    uint8_t battery;
    uint8_t charging; /* actively charging (not full) */
    uint8_t chg_full; /* charge complete */
    uint8_t blink;    /* 0/1 — charging segment pulse */
    uint8_t anim;     /* phase for idle/away motion (eyes, etc.) */
    uint8_t linked;
    uint8_t time_valid;
    uint8_t hour;
    uint8_t minute;
    uint16_t osd_color; /* RGB565 when mode == VK_UI_OSD */
    char osd_text[9];   /* NUL-terminated, ≤8 glyphs */
} vk_ui_model_t;

void vk_ui_render(uint16_t *fb, const vk_ui_model_t *m);
int vk_ui_write_ppm(FILE *fp, const uint16_t *fb);
