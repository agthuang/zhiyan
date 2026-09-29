#include "vk_protocol.h"
#include "vk_battery.h"
#include "vk_keys.h"
#include "vk_hid.h"
#include "vk_ui.h"
#include "vk_idle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_fail++;
    }
}

static void test_protocol(void)
{
    uint8_t buf[32];
    vk_key_t key = {.key = VK_KEY_YES, .action = VK_ACT_DOWN};
    size_t n = vk_pack(buf, sizeof(buf), VK_PKT_KEY, 7, &key, sizeof(key));
    expect(n == VK_HDR_SIZE + sizeof(key), "pack size");

    vk_hdr_t hdr;
    const uint8_t *payload = NULL;
    expect(vk_parse(buf, n, &hdr, &payload), "parse ok");
    expect(hdr.type == VK_PKT_KEY && hdr.seq == 7, "hdr fields");
    expect(payload != NULL && payload[0] == VK_KEY_YES, "payload");

    buf[0] ^= 0xFF;
    expect(!vk_parse(buf, n, &hdr, &payload), "bad magic rejected");

    expect(!vk_unix_is_valid(0), "boot time invalid");
    expect(vk_unix_is_valid(1736899200u), "2025 valid");

    uint8_t h = 0, m = 0;
    vk_unix_to_hm(1736899200u, 480, &h, &m); /* 2025-01-15 00:00 UTC -> 08:00 CST */
    expect(h == 8, "tz hour");

    vk_backlight_t bl = {.duty = VK_BL_DEFAULT};
    n = vk_pack(buf, sizeof(buf), VK_PKT_BACKLIGHT, 1, &bl, sizeof(bl));
    expect(n == VK_HDR_SIZE + sizeof(bl), "backlight pack");
    expect(vk_parse(buf, n, &hdr, &payload), "backlight parse");
    expect(hdr.type == VK_PKT_BACKLIGHT && payload && payload[0] == VK_BL_DEFAULT, "backlight duty");

    expect(vk_rgb888_to_565(0xEC, 0xB0, 0x48) != 0, "rgb565 non-zero");
    char word[9];
    expect(vk_osd_sanitize(word, sizeof(word), "th!nk?") == 6 && strcmp(word, "TH!NK?") == 0, "sanitize");
    expect(vk_osd_sanitize(word, sizeof(word), "toolongword") == 8, "truncate");

    vk_osd_t osd = {.color = vk_rgb888_to_565(0x6E, 0xC4, 0x8C), .flags = 0, .len = 4};
    memcpy(osd.text, "CODE", 4);
    n = vk_pack(buf, sizeof(buf), VK_PKT_OSD, 2, &osd, sizeof(osd));
    expect(n == VK_HDR_SIZE + sizeof(osd), "osd pack");
    expect(vk_parse(buf, n, &hdr, &payload), "osd parse");
    expect(hdr.type == VK_PKT_OSD, "osd type");

    vk_eco_t eco = {.enabled = 1};
    n = vk_pack(buf, sizeof(buf), VK_PKT_ECO, 3, &eco, sizeof(eco));
    expect(n == VK_HDR_SIZE + sizeof(eco), "eco pack");
    expect(vk_parse(buf, n, &hdr, &payload), "eco parse");
    expect(hdr.type == VK_PKT_ECO && payload && payload[0] == 1, "eco enabled");
}

static void test_battery(void)
{
    expect(vk_battery_percent(3300) == 0, "empty");
    expect(vk_battery_percent(4150) == 100, "full");
    expect(vk_battery_percent(3725) > 40 && vk_battery_percent(3725) < 70, "mid");
    int vbat = vk_battery_from_sense_mv(1040);
    expect(vbat > 4000 && vbat < 4300, "divider undo");
}

static void test_keys(void)
{
    vk_keys_t k;
    vk_keys_init(&k);
    uint8_t ev = 0;
    for (int i = 0; i < 3; i++) {
        ev = vk_keys_feed(&k, VK_KEYMASK_YES, (uint32_t)(i * 5));
    }
    expect(ev & VK_EV_YES_DOWN, "yes down");
    expect(!(ev & VK_EV_YES_UP), "no up yet");

    for (int i = 0; i < 3; i++) {
        ev = vk_keys_feed(&k, 0, (uint32_t)(20 + i * 5));
    }
    expect(ev & VK_EV_YES_UP, "yes up");

    vk_keys_init(&k);
    ev = 0;
    for (uint32_t t = 0; t <= 2600; t += 5) {
        ev |= vk_keys_feed(&k, (uint8_t)(VK_KEYMASK_YES | VK_KEYMASK_NO), t);
    }
    expect(ev & VK_EV_UNPAIR, "unpair combo");
}

static void test_hid(void)
{
    vk_hid_report_t r;
    vk_hid_map_t def;
    vk_hid_map_default(&def);
    vk_hid_map_set(&def);

    expect(vk_hid_from_key(VK_KEY_VOICE, VK_ACT_DOWN, &r), "voice down");
    expect(r.modifiers == (VK_HID_MOD_RCTRL | VK_HID_MOD_RGUI) && r.pulse == 0, "doubao chord");
    expect(vk_hid_from_key(VK_KEY_VOICE, VK_ACT_UP, &r) && r.modifiers == 0, "voice up clears");
    expect(vk_hid_from_key(VK_KEY_YES, VK_ACT_DOWN, &r) && r.keycode == VK_HID_ENTER && !r.pulse, "enter hold");
    expect(vk_hid_from_key(VK_KEY_YES, VK_ACT_UP, &r) && r.modifiers == 0 && r.keycode == 0, "yes up clears");
    expect(vk_hid_from_key(VK_KEY_NO, VK_ACT_DOWN, &r) && r.keycode == VK_HID_BACKSPACE && r.pulse == 0, "backspace hold");
    expect(vk_hid_from_key(VK_KEY_NO, VK_ACT_UP, &r) && r.modifiers == 0 && r.keycode == 0, "no up clears");

    vk_hid_map_t custom = def;
    custom.yes.modifiers = VK_HID_MOD_LGUI;
    custom.yes.keycode = 0x2C; /* space */
    vk_hid_map_set(&custom);
    expect(vk_hid_from_key(VK_KEY_YES, VK_ACT_DOWN, &r) && r.modifiers == VK_HID_MOD_LGUI && r.keycode == 0x2C,
           "custom yes");
    vk_hid_map_set(&def);
}

static int write_preview(const char *path, const vk_ui_model_t *m)
{
    uint16_t fb[VK_LCD_W * VK_LCD_H];
    vk_ui_render(fb, m);
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return -1;
    }
    int rc = vk_ui_write_ppm(fp, fb);
    fclose(fp);
    return rc;
}

static void test_ui(const char *preview_dir)
{
    uint16_t fb[VK_LCD_W * VK_LCD_H];
    vk_ui_model_t m = {
        .mode = VK_UI_IDLE,
        .battery = 95,
        .charging = 0,
        .chg_full = 0,
        .blink = 0,
        .linked = 1,
        .time_valid = 1,
        .hour = 21,
        .minute = 48,
    };
    vk_ui_render(fb, &m);
    expect(fb[0] == fb[1], "bg consistent");
    /* Center of a clock cell should not be background. */
    uint16_t bg = fb[0];
    int found = 0;
    for (int i = 0; i < VK_LCD_W * VK_LCD_H; i++) {
        if (fb[i] != bg) {
            found++;
        }
    }
    expect(found > 80, "idle has foreground pixels");

    m.time_valid = 0;
    m.anim = 6;
    vk_ui_render(fb, &m);
    found = 0;
    for (int i = 0; i < VK_LCD_W * VK_LCD_H; i++) {
        if (fb[i] != bg) {
            found++;
        }
    }
    expect(found > 80, "wait pup has foreground pixels");
    m.time_valid = 1;

    if (preview_dir) {
        char path[256];
        /* Same battery on every frame so docs look consistent. */
        m.battery = 95;
        m.charging = 0;
        m.chg_full = 0;

        snprintf(path, sizeof(path), "%s/idle.ppm", preview_dir);
        expect(write_preview(path, &m) == 0, "write idle");

        m.time_valid = 0;
        m.anim = 6;
        snprintf(path, sizeof(path), "%s/wait.ppm", preview_dir);
        expect(write_preview(path, &m) == 0, "write wait");
        m.time_valid = 1;

        m.mode = VK_UI_TALK;
        snprintf(path, sizeof(path), "%s/talk.ppm", preview_dir);
        expect(write_preview(path, &m) == 0, "write talk");

        m.mode = VK_UI_YES;
        snprintf(path, sizeof(path), "%s/yes.ppm", preview_dir);
        expect(write_preview(path, &m) == 0, "write yes");

        m.mode = VK_UI_NO;
        snprintf(path, sizeof(path), "%s/no.ppm", preview_dir);
        expect(write_preview(path, &m) == 0, "write no");

        m.mode = VK_UI_PAIR;
        m.linked = 0;
        m.time_valid = 0;
        snprintf(path, sizeof(path), "%s/pair.ppm", preview_dir);
        expect(write_preview(path, &m) == 0, "write pair");

        m.mode = VK_UI_AWAY;
        m.linked = 0;
        m.time_valid = 0;
        m.anim = 5; /* eyes open, slight look */
        snprintf(path, sizeof(path), "%s/away.ppm", preview_dir);
        expect(write_preview(path, &m) == 0, "write away");

        /* Codex OSD words (aihook/codex/osd_cdc.py colors). */
        m.mode = VK_UI_OSD;
        m.linked = 1;
        m.time_valid = 1;

        m.osd_color = vk_rgb888_to_565(0xFF, 0xB0, 0x20);
        snprintf(m.osd_text, sizeof(m.osd_text), "THINK...");
        snprintf(path, sizeof(path), "%s/osd.ppm", preview_dir);
        expect(write_preview(path, &m) == 0, "write osd");

        m.osd_color = vk_rgb888_to_565(0x2A, 0xD4, 0xFF);
        snprintf(m.osd_text, sizeof(m.osd_text), "WORKING");
        snprintf(path, sizeof(path), "%s/working.ppm", preview_dir);
        expect(write_preview(path, &m) == 0, "write working");

        m.osd_color = vk_rgb888_to_565(0xFF, 0x4D, 0x6A);
        snprintf(m.osd_text, sizeof(m.osd_text), "ASK?");
        snprintf(path, sizeof(path), "%s/ask.ppm", preview_dir);
        expect(write_preview(path, &m) == 0, "write ask");

        m.osd_color = vk_rgb888_to_565(0xB7, 0x94, 0xF6);
        snprintf(m.osd_text, sizeof(m.osd_text), "INPUT");
        snprintf(path, sizeof(path), "%s/input.ppm", preview_dir);
        expect(write_preview(path, &m) == 0, "write input");
    }
}

static void test_idle(void)
{
    vk_idle_t idle;
    vk_idle_init(&idle, false, 0);
    expect(!vk_idle_is_blanked(&idle), "disabled starts unblanked");
    expect(!vk_idle_tick(&idle, 60000), "disabled never blanks");
    expect(!vk_idle_is_blanked(&idle), "still unblanked");

    vk_idle_init(&idle, true, 1000);
    expect(!vk_idle_is_blanked(&idle), "enabled starts unblanked");
    expect(!vk_idle_tick(&idle, 1000 + VK_IDLE_BLANK_MS_DEFAULT - 1), "not yet");
    expect(vk_idle_tick(&idle, 1000 + VK_IDLE_BLANK_MS_DEFAULT), "blank at timeout");
    expect(vk_idle_is_blanked(&idle), "blanked");
    expect(!vk_idle_tick(&idle, 1000 + VK_IDLE_BLANK_MS_DEFAULT + 500), "no change while blank");

    vk_idle_activity(&idle, 50000);
    expect(!vk_idle_is_blanked(&idle), "activity wakes");
    expect(!vk_idle_tick(&idle, 50000 + 1000), "timer reset");
    expect(vk_idle_tick(&idle, 50000 + VK_IDLE_BLANK_MS_DEFAULT), "blanks again");

    vk_idle_set_enabled(&idle, false, 90000);
    expect(!vk_idle_is_blanked(&idle), "disable wakes");
    expect(!vk_idle_tick(&idle, 90000 + VK_IDLE_BLANK_MS_DEFAULT + 1000), "stay on when off");
}

int main(int argc, char **argv)
{
    const char *preview = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--preview") == 0 && i + 1 < argc) {
            preview = argv[++i];
        }
    }

    test_protocol();
    test_battery();
    test_keys();
    test_hid();
    test_ui(preview);
    test_idle();

    if (g_fail) {
        fprintf(stderr, "%d test(s) failed\n", g_fail);
        return 1;
    }
    puts("ok");
    return 0;
}
