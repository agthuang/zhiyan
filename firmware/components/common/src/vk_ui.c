#include "vk_ui.h"

#include <string.h>

#define RGB565(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

/* Ink / paper — dark base; accents use the color LCD. */
static const uint16_t COL_BG     = RGB565(11, 12, 16);
static const uint16_t COL_TIME   = RGB565(220, 188, 140); /* warm sand clock */
static const uint16_t COL_MUTED  = RGB565(110, 116, 128);
static const uint16_t COL_DIM    = RGB565(48, 52, 62);
static const uint16_t COL_FRAME  = RGB565(160, 168, 180);
static const uint16_t COL_SAGE   = RGB565(96, 196, 132);  /* linked / healthy bat */
static const uint16_t COL_CLAY   = RGB565(220, 96, 88);   /* low battery */
static const uint16_t COL_WARM   = RGB565(236, 176, 72);  /* charging / amber */
static const uint16_t COL_TALK   = RGB565(232, 152, 120);
static const uint16_t COL_YES    = RGB565(110, 200, 140);
static const uint16_t COL_NO     = RGB565(232, 120, 108);
static const uint16_t COL_SEG_OFF = RGB565(28, 32, 40);

/* 3x5 digits, MSB is left column of each row nibble (bits 2..0). */
static const uint8_t DIGIT_3X5[10][5] = {
    {0x7, 0x5, 0x5, 0x5, 0x7}, /* 0 */
    {0x2, 0x6, 0x2, 0x2, 0x7}, /* 1 */
    {0x7, 0x1, 0x7, 0x4, 0x7}, /* 2 */
    {0x7, 0x1, 0x7, 0x1, 0x7}, /* 3 */
    {0x5, 0x5, 0x7, 0x1, 0x1}, /* 4 */
    {0x7, 0x4, 0x7, 0x1, 0x7}, /* 5 */
    {0x7, 0x4, 0x7, 0x5, 0x7}, /* 6 */
    {0x7, 0x1, 0x1, 0x1, 0x1}, /* 7 */
    {0x7, 0x5, 0x7, 0x5, 0x7}, /* 8 */
    {0x7, 0x5, 0x7, 0x1, 0x7}, /* 9 */
};

/* 5x7 column-major, bit0 = top. Covers ' ' .. 'z'. */
static const uint8_t FONT5X7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* space */
    {0x00, 0x00, 0x5F, 0x00, 0x00}, /* ! */
    {0x00, 0x07, 0x00, 0x07, 0x00},
    {0x14, 0x7F, 0x14, 0x7F, 0x14},
    {0x24, 0x2A, 0x7F, 0x2A, 0x12},
    {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x55, 0x22, 0x50},
    {0x00, 0x05, 0x03, 0x00, 0x00},
    {0x00, 0x1C, 0x22, 0x41, 0x00},
    {0x00, 0x41, 0x22, 0x1C, 0x00},
    {0x14, 0x08, 0x3E, 0x08, 0x14},
    {0x08, 0x08, 0x3E, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00},
    {0x08, 0x08, 0x08, 0x08, 0x08}, /* - */
    {0x00, 0x60, 0x60, 0x00, 0x00},
    {0x20, 0x10, 0x08, 0x04, 0x02},
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0 */
    {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30},
    {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 9 */
    {0x00, 0x36, 0x36, 0x00, 0x00}, /* : */
    {0x00, 0x56, 0x36, 0x00, 0x00},
    {0x08, 0x14, 0x22, 0x41, 0x00},
    {0x14, 0x14, 0x14, 0x14, 0x14},
    {0x00, 0x41, 0x22, 0x14, 0x08},
    {0x02, 0x01, 0x51, 0x09, 0x06},
    {0x32, 0x49, 0x79, 0x41, 0x3E},
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* A */
    {0x7F, 0x49, 0x49, 0x49, 0x36},
    {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C},
    {0x7F, 0x49, 0x49, 0x49, 0x41},
    {0x7F, 0x09, 0x09, 0x09, 0x01},
    {0x3E, 0x41, 0x49, 0x49, 0x7A},
    {0x7F, 0x08, 0x08, 0x08, 0x7F},
    {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01},
    {0x7F, 0x08, 0x14, 0x22, 0x41},
    {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    {0x7F, 0x04, 0x08, 0x10, 0x7F},
    {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06},
    {0x3E, 0x41, 0x51, 0x21, 0x5E},
    {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7F, 0x01, 0x01},
    {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F},
    {0x3F, 0x40, 0x38, 0x40, 0x3F},
    {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07},
    {0x61, 0x51, 0x49, 0x45, 0x43}, /* Z */
    {0x00, 0x7F, 0x41, 0x41, 0x00},
    {0x02, 0x04, 0x08, 0x10, 0x20},
    {0x00, 0x41, 0x41, 0x7F, 0x00},
    {0x04, 0x02, 0x01, 0x02, 0x04},
    {0x40, 0x40, 0x40, 0x40, 0x40},
    {0x00, 0x01, 0x02, 0x04, 0x00},
    {0x20, 0x54, 0x54, 0x54, 0x78}, /* a */
    {0x7F, 0x48, 0x44, 0x44, 0x38},
    {0x38, 0x44, 0x44, 0x44, 0x20},
    {0x38, 0x44, 0x44, 0x48, 0x7F},
    {0x38, 0x54, 0x54, 0x54, 0x18},
    {0x08, 0x7E, 0x09, 0x01, 0x02},
    {0x0C, 0x52, 0x52, 0x52, 0x3E},
    {0x7F, 0x08, 0x04, 0x04, 0x78},
    {0x00, 0x44, 0x7D, 0x40, 0x00},
    {0x20, 0x40, 0x44, 0x3D, 0x00},
    {0x7F, 0x10, 0x28, 0x44, 0x00},
    {0x00, 0x41, 0x7F, 0x40, 0x00},
    {0x7C, 0x04, 0x18, 0x04, 0x78},
    {0x7C, 0x08, 0x04, 0x04, 0x78},
    {0x38, 0x44, 0x44, 0x44, 0x38},
    {0x7C, 0x14, 0x14, 0x14, 0x08},
    {0x08, 0x14, 0x14, 0x18, 0x7C},
    {0x7C, 0x08, 0x04, 0x04, 0x08},
    {0x48, 0x54, 0x54, 0x54, 0x20},
    {0x04, 0x3F, 0x44, 0x40, 0x20},
    {0x3C, 0x40, 0x40, 0x20, 0x7C},
    {0x1C, 0x20, 0x40, 0x20, 0x1C},
    {0x3C, 0x40, 0x30, 0x40, 0x3C},
    {0x44, 0x28, 0x10, 0x28, 0x44},
    {0x0C, 0x50, 0x50, 0x50, 0x3C},
    {0x44, 0x64, 0x54, 0x4C, 0x44}, /* z */
};

static uint16_t *px(uint16_t *fb, int x, int y)
{
    return &fb[y * VK_LCD_W + x];
}

static void put(uint16_t *fb, int x, int y, uint16_t c)
{
    if ((unsigned)x < VK_LCD_W && (unsigned)y < VK_LCD_H) {
        *px(fb, x, y) = c;
    }
}

static void fill(uint16_t *fb, uint16_t c)
{
    for (int i = 0; i < VK_LCD_W * VK_LCD_H; i++) {
        fb[i] = c;
    }
}

static void fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t c)
{
    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) {
            put(fb, x + xx, y + yy, c);
        }
    }
}

static const uint8_t *glyph5(char ch)
{
    unsigned idx;
    if (ch < 32 || ch > 'z') {
        idx = 0;
    } else {
        idx = (unsigned)(ch - 32);
        if (idx >= sizeof(FONT5X7) / sizeof(FONT5X7[0])) {
            idx = 0;
        }
    }
    return FONT5X7[idx];
}

static int text_width(const char *s, int scale, int gap)
{
    int n = (int)strlen(s);
    if (n == 0) {
        return 0;
    }
    return n * (5 * scale) + (n - 1) * gap;
}

static void draw_char(uint16_t *fb, int x, int y, char ch, int scale, uint16_t c)
{
    const uint8_t *g = glyph5(ch);
    for (int col = 0; col < 5; col++) {
        uint8_t bits = g[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1u << row)) {
                fill_rect(fb, x + col * scale, y + row * scale, scale, scale, c);
            }
        }
    }
}

static void draw_text(uint16_t *fb, int x, int y, const char *s, int scale, int gap, uint16_t c)
{
    for (; *s; s++) {
        draw_char(fb, x, y, *s, scale, c);
        x += 5 * scale + gap;
    }
}

static void draw_text_centered(uint16_t *fb, int y, const char *s, int scale, int gap, uint16_t c)
{
    int w = text_width(s, scale, gap);
    draw_text(fb, (VK_LCD_W - w) / 2, y, s, scale, gap, c);
}

static void draw_block(uint16_t *fb, int x, int y, int cell, int inset, uint16_t c)
{
    int s = cell - inset;
    if (s < 1) {
        s = 1;
    }
    fill_rect(fb, x, y, s, s, c);
}

static void draw_digit(uint16_t *fb, int x, int y, int digit, int cell, uint16_t c)
{
    if (digit < 0 || digit > 9) {
        return;
    }
    for (int row = 0; row < 5; row++) {
        uint8_t bits = DIGIT_3X5[digit][row];
        for (int col = 0; col < 3; col++) {
            if (bits & (1u << (2 - col))) {
                draw_block(fb, x + col * cell, y + row * cell, cell, 1, c);
            }
        }
    }
}

static void draw_colon(uint16_t *fb, int x, int y, int cell, uint16_t c)
{
    int s = cell - 1;
    if (s < 2) {
        s = 2;
    }
    fill_rect(fb, x, y + cell, s, s, c);
    fill_rect(fb, x, y + cell * 3, s, s, c);
}

#include "vk_cat_sprite.inc"

/**
 * Cute loaf cat sprite for unlinked/away.
 * anim: gentle bob + blink so lcd dirty-check refreshes.
 */
static void draw_away_cat(uint16_t *fb, uint8_t anim)
{
    int bob = ((anim / 10) % 2);
    int ox = (VK_LCD_W - VK_CAT_W) / 2;
    int oy = 11 + bob;
    int blink = ((anim % 36) >= 32);

    for (int y = 0; y < VK_CAT_H; y++) {
        for (int x = 0; x < VK_CAT_W; x++) {
            unsigned i = (unsigned)y * VK_CAT_W + (unsigned)x;
            uint8_t byte = vk_cat_pix[i / 2];
            uint8_t idx = (i & 1u) ? (byte & 0x0Fu) : (byte >> 4);
            if (idx != 0 && idx < VK_CAT_NCOL) {
                put(fb, ox + x, oy + y, vk_cat_pal[idx]);
            }
        }
    }

    if (blink) {
        /* Soft lids — eye centers on the 70x58 loaf sprite. */
        const uint16_t lid = vk_cat_pal[4]; /* cream */
        fill_rect(fb, ox + 22, oy + 20, 10, 3, lid);
        fill_rect(fb, ox + 40, oy + 20, 10, 3, lid);
    }
}

static void draw_clock(uint16_t *fb, int hour, int minute, int valid)
{
    const int cell = 6;
    const int gw = 3 * cell;
    const int gh = 5 * cell;
    const int gap = 4;
    const int colon_w = cell;
    int total = gw * 4 + colon_w + gap * 4;
    int x = (VK_LCD_W - total) / 2;
    int y = 26;
    uint16_t c = valid ? COL_TIME : COL_MUTED;

    if (!valid) {
        /* "--:--" using minus bars */
        for (int i = 0; i < 4; i++) {
            int gx = x + i * (gw + gap);
            if (i >= 2) {
                gx += colon_w + gap;
            }
            fill_rect(fb, gx + cell / 2, y + 2 * cell, gw - cell, cell - 1, c);
        }
        draw_colon(fb, x + 2 * (gw + gap), y, cell, COL_DIM);
        return;
    }

    draw_digit(fb, x, y, hour / 10, cell, c);
    x += gw + gap;
    draw_digit(fb, x, y, hour % 10, cell, c);
    x += gw + gap;
    draw_colon(fb, x, y, cell, COL_WARM);
    x += colon_w + gap;
    draw_digit(fb, x, y, minute / 10, cell, c);
    x += gw + gap;
    draw_digit(fb, x, y, minute % 10, cell, c);

    (void)gh;
}

static void draw_pip(uint16_t *fb, int linked, int pairing)
{
    int x = 8;
    int y = 6;
    uint16_t fillc = pairing ? COL_WARM : (linked ? COL_SAGE : COL_DIM);
    /* 5x5 rounded pip */
    fill_rect(fb, x + 1, y, 3, 5, fillc);
    fill_rect(fb, x, y + 1, 5, 3, fillc);
    if (!linked && !pairing) {
        fill_rect(fb, x + 1, y + 1, 3, 3, COL_BG);
        put(fb, x + 2, y + 2, COL_MUTED);
    }
}

static void draw_bolt(uint16_t *fb, int x, int y, uint16_t c)
{
    /* Tiny lightning in battery body (5x7). */
    put(fb, x + 2, y, c);
    put(fb, x + 2, y + 1, c);
    put(fb, x + 1, y + 2, c);
    put(fb, x + 2, y + 2, c);
    put(fb, x + 3, y + 2, c);
    put(fb, x + 2, y + 3, c);
    put(fb, x + 1, y + 3, c);
    put(fb, x + 2, y + 4, c);
    put(fb, x + 3, y + 5, c);
    put(fb, x + 2, y + 6, c);
}

static uint16_t bat_level_color(int pct)
{
    if (pct < 20) {
        return COL_CLAY;
    }
    if (pct < 50) {
        return COL_WARM;
    }
    return COL_SAGE;
}

static void draw_battery(uint16_t *fb, int pct, int charging, int chg_full, int blink)
{
    if (pct < 0) {
        pct = 0;
    }
    if (pct > 100) {
        pct = 100;
    }

    uint16_t fillc;
    if (chg_full) {
        fillc = COL_SAGE;
        pct = 100;
    } else if (charging) {
        fillc = COL_WARM;
        if (pct < 1) {
            pct = 1; /* keep at least a pulse while charging */
        }
    } else {
        fillc = bat_level_color(pct);
    }

    /* Percent text left of icon (after status overrides). */
    char label[8];
    int n = 0;
    if (pct >= 100) {
        label[n++] = '1';
        label[n++] = '0';
        label[n++] = '0';
    } else {
        if (pct >= 10) {
            label[n++] = (char)('0' + pct / 10);
        }
        label[n++] = (char)('0' + pct % 10);
    }
    label[n++] = '%';
    label[n] = 0;
    int tw = text_width(label, 1, 1);
    int icon_x = 128;
    int icon_y = 4;
    int icon_w = 26;
    int icon_h = 12;
    draw_text(fb, icon_x - tw - 3, 6, label, 1, 1, chg_full ? COL_SAGE : (charging ? COL_WARM : COL_FRAME));

    uint16_t frame = COL_FRAME;

    /* Outline (2px-ish via double lines) */
    fill_rect(fb, icon_x, icon_y, icon_w, 1, frame);
    fill_rect(fb, icon_x, icon_y + icon_h - 1, icon_w, 1, frame);
    fill_rect(fb, icon_x, icon_y, 1, icon_h, frame);
    fill_rect(fb, icon_x + icon_w - 1, icon_y, 1, icon_h, frame);
    fill_rect(fb, icon_x + 1, icon_y + 1, icon_w - 2, 1, COL_DIM);
    fill_rect(fb, icon_x + 1, icon_y + icon_h - 2, icon_w - 2, 1, COL_DIM);
    /* nub */
    fill_rect(fb, icon_x + icon_w, icon_y + 3, 3, icon_h - 6, frame);

    /* 4 segments: 1–24 / 25–49 / 50–74 / 75–100 */
    int segs = 0;
    if (pct >= 75) {
        segs = 4;
    } else if (pct >= 50) {
        segs = 3;
    } else if (pct >= 25) {
        segs = 2;
    } else if (pct > 0) {
        segs = 1;
    }
    if (chg_full) {
        segs = 4;
    }
    if (charging && blink && segs < 4) {
        segs++;
    }

    const int gap = 1;
    const int seg_w = 4;
    const int seg_h = icon_h - 6;
    int sx0 = icon_x + 3;
    int sy = icon_y + 3;
    for (int i = 0; i < 4; i++) {
        int sx = sx0 + i * (seg_w + gap);
        uint16_t c = (i < segs) ? fillc : COL_SEG_OFF;
        fill_rect(fb, sx, sy, seg_w, seg_h, c);
    }

    if (charging && !chg_full) {
        draw_bolt(fb, icon_x + 10, icon_y + 2, COL_TIME);
    }
}

static void draw_status_bar(uint16_t *fb, const vk_ui_model_t *m)
{
    uint16_t c = COL_DIM;
    if (m->mode == VK_UI_PAIR) {
        c = COL_WARM;
    } else if (m->linked) {
        c = COL_SAGE;
    }
    fill_rect(fb, 20, 74, 120, 2, c);
}

void vk_ui_render(uint16_t *fb, const vk_ui_model_t *m)
{
    fill(fb, COL_BG);
    draw_pip(fb, m->linked, m->mode == VK_UI_PAIR);
    draw_battery(fb, m->battery, m->charging, m->chg_full, m->blink);
    draw_status_bar(fb, m);

    switch (m->mode) {
    case VK_UI_PAIR:
        draw_text_centered(fb, 32, "pair", 3, 3, COL_WARM);
        break;
    case VK_UI_TALK:
        draw_text_centered(fb, 30, "talk", 3, 3, COL_TALK);
        fill_rect(fb, 48, 60, 64, 3, COL_DIM);
        fill_rect(fb, 48, 60, 40, 3, COL_TALK);
        break;
    case VK_UI_YES:
        draw_text_centered(fb, 30, "yes", 3, 4, COL_YES);
        break;
    case VK_UI_NO:
        draw_text_centered(fb, 30, "no", 3, 4, COL_NO);
        break;
    case VK_UI_AWAY:
        /* No clock while unlinked — time drifts; show waiting cat pet. */
        draw_away_cat(fb, m->anim);
        break;
    case VK_UI_MEDIA:
        draw_text_centered(fb, 30, "bt", 3, 4, m->linked ? COL_SAGE : COL_WARM);
        draw_text_centered(fb, 64, m->linked ? "phone" : "Zhiyan", 1, 1,
                           COL_MUTED);
        break;
    case VK_UI_OSD: {
        const char *t = m->osd_text[0] ? m->osd_text : "?";
        int scale = (int)strlen(t) > 5 ? 2 : 3;
        int gap = scale >= 3 ? 3 : 2;
        uint16_t c = m->osd_color ? m->osd_color : COL_WARM;
        draw_text_centered(fb, 28, t, scale, gap, c);
        /* Color bar under the word — reads better than tint alone on tiny LCD. */
        fill_rect(fb, 36, 58, 88, 4, c);
        fill_rect(fb, 48, 64, 64, 2, COL_DIM);
        break;
    }
    case VK_UI_IDLE:
    default:
        draw_clock(fb, m->hour, m->minute, m->time_valid);
        break;
    }
}

int vk_ui_write_ppm(FILE *fp, const uint16_t *fb)
{
    if (fp == NULL || fb == NULL) {
        return -1;
    }
    if (fprintf(fp, "P6\n%d %d\n255\n", VK_LCD_W, VK_LCD_H) < 0) {
        return -1;
    }
    for (int i = 0; i < VK_LCD_W * VK_LCD_H; i++) {
        uint16_t c = fb[i];
        uint8_t rgb[3] = {
            (uint8_t)(((c >> 11) & 0x1F) * 255 / 31),
            (uint8_t)(((c >> 5) & 0x3F) * 255 / 63),
            (uint8_t)((c & 0x1F) * 255 / 31),
        };
        if (fwrite(rgb, 1, 3, fp) != 3) {
            return -1;
        }
    }
    return 0;
}
