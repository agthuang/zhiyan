#include "vk_usb.h"

#include "audio_ring.h"
#include "radio.h"
#include "usb_descriptors.h"
#include "vk_protocol.h"

#include "tusb.h"
#include "class/audio/audio_device.h"
#include "esp_log.h"
#include "esp_private/usb_phy.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static const char *TAG = "usb";

static usb_phy_handle_t s_phy;
static SemaphoreHandle_t s_hid_mu;
static uint32_t s_unix;
static uint8_t s_bl_duty = VK_BL_DEFAULT;
static uint8_t s_idle_blank; /* 0/1 — persist + push to handheld */
static uint8_t s_hh_battery;
static uint8_t s_hh_flags;
static uint32_t s_hh_last_ms;
static bool s_hh_seen;
static bool s_mute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
static uint16_t s_volume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];
static uint32_t s_samp_freq = CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;
static uint8_t s_clk_valid = 1;
static audio_control_range_4_n_t(1) s_sample_freq_rng;
static int16_t s_audio_buf[(CFG_TUD_AUDIO_EP_SZ_IN - 2) / 2];

static void bl_nvs_save(uint8_t duty)
{
    nvs_handle_t h;
    if (nvs_open("vk", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u8(h, "bl", duty);
    nvs_commit(h);
    nvs_close(h);
}

static void bl_nvs_load(void)
{
    nvs_handle_t h;
    if (nvs_open("vk", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t duty = VK_BL_DEFAULT;
    if (nvs_get_u8(h, "bl", &duty) == ESP_OK) {
        s_bl_duty = duty;
    }
    nvs_close(h);
}

static bool bl_send(uint8_t duty)
{
    vk_backlight_t bl = {.duty = duty};
    return rx_radio_send(VK_PKT_BACKLIGHT, &bl, sizeof(bl));
}

static void idle_nvs_save(uint8_t enabled)
{
    nvs_handle_t h;
    if (nvs_open("vk", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u8(h, "idle", enabled ? 1u : 0u);
    nvs_commit(h);
    nvs_close(h);
}

static void idle_nvs_load(void)
{
    nvs_handle_t h;
    if (nvs_open("vk", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t v = 0;
    if (nvs_get_u8(h, "idle", &v) == ESP_OK) {
        s_idle_blank = v ? 1u : 0u;
    }
    nvs_close(h);
}

static bool idle_send(uint8_t enabled)
{
    vk_idle_blank_t cfg = {
        .enabled = enabled ? 1u : 0u,
        .timeout_sec = 0, /* handheld default 20s */
    };
    return rx_radio_send(VK_PKT_IDLE_BLANK, &cfg, sizeof(cfg));
}

static void parse_idle_cmd(char *rest)
{
    while (*rest == ' ' || *rest == '\t' || *rest == '=') {
        rest++;
    }
    if (*rest == '?' || *rest == 0) {
        vk_usb_cdc_printf("I=%u\r\n", (unsigned)s_idle_blank);
        return;
    }
    int on = -1;
    if (*rest == '1' || *rest == 'y' || *rest == 'Y') {
        on = 1;
    } else if (*rest == '0' || *rest == 'n' || *rest == 'N') {
        on = 0;
    } else if (strncmp(rest, "on", 2) == 0) {
        on = 1;
    } else if (strncmp(rest, "off", 3) == 0) {
        on = 0;
    }
    if (on < 0) {
        vk_usb_cdc_printf("I? | I 0|1 | I on|off\r\n");
        return;
    }
    s_idle_blank = (uint8_t)on;
    idle_nvs_save(s_idle_blank);
    bool sent = idle_send(s_idle_blank);
    vk_usb_cdc_printf("I ok%s\r\n", sent ? "" : " (no peer)");
    vk_usb_cdc_printf("I=%u\r\n", (unsigned)s_idle_blank);
}

static bool osd_send(const vk_osd_t *osd)
{
    return rx_radio_send(VK_PKT_OSD, osd, sizeof(*osd));
}

static void parse_osd_cmd(char *rest)
{
    while (*rest == ' ' || *rest == '\t') {
        rest++;
    }
    if (*rest == '!') {
        vk_osd_t osd = {.flags = VK_OSD_FLAG_CLEAR, .len = 0};
        bool sent = osd_send(&osd);
        vk_usb_cdc_printf("O cleared%s\r\n", sent ? "" : " (no peer)");
        return;
    }
    if (*rest != '#') {
        vk_usb_cdc_printf("O #RRGGBB WORD | O!\r\n");
        return;
    }
    rest++;
    if (strlen(rest) < 6) {
        vk_usb_cdc_printf("O err color\r\n");
        return;
    }
    char hex[7];
    memcpy(hex, rest, 6);
    hex[6] = 0;
    for (int i = 0; i < 6; i++) {
        char c = hex[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            vk_usb_cdc_printf("O err color\r\n");
            return;
        }
    }
    unsigned long rgb = strtoul(hex, NULL, 16);
    uint8_t r = (uint8_t)((rgb >> 16) & 0xFFu);
    uint8_t g = (uint8_t)((rgb >> 8) & 0xFFu);
    uint8_t b = (uint8_t)(rgb & 0xFFu);
    rest += 6;
    while (*rest == ' ' || *rest == '\t') {
        rest++;
    }
    if (*rest == 0) {
        vk_usb_cdc_printf("O err word\r\n");
        return;
    }
    vk_osd_t osd = {0};
    osd.color = vk_rgb888_to_565(r, g, b);
    char tmp[VK_OSD_TEXT_MAX + 1];
    osd.len = vk_osd_sanitize(tmp, sizeof(tmp), rest);
    if (osd.len == 0) {
        vk_usb_cdc_printf("O err word\r\n");
        return;
    }
    memcpy(osd.text, tmp, VK_OSD_TEXT_MAX);
    bool sent = osd_send(&osd);
    vk_usb_cdc_printf("O ok %s%s\r\n", osd.text, sent ? "" : " (no peer)");
}

static void usb_task(void *arg)
{
    (void)arg;
    while (true) {
        tud_task();
    }
}

static void phy_init(void)
{
    usb_phy_config_t cfg = {
        .controller = USB_PHY_CTRL_OTG,
        .target = USB_PHY_TARGET_INT,
        .otg_mode = USB_OTG_MODE_DEVICE,
        .otg_speed = USB_PHY_SPEED_FULL,
    };
    ESP_ERROR_CHECK(usb_new_phy(&cfg, &s_phy));
}

static void time_nvs_save(uint32_t unix_time)
{
    nvs_handle_t h;
    if (nvs_open("vk", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u32(h, "unix", unix_time);
    nvs_commit(h);
    nvs_close(h);
}

static bool time_nvs_load(uint32_t *out)
{
    nvs_handle_t h;
    if (nvs_open("vk", NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_get_u32(h, "unix", out);
    nvs_close(h);
    return err == ESP_OK && vk_unix_is_valid(*out);
}

void vk_usb_set_unix_time(uint32_t unix_time)
{
    if (!vk_unix_is_valid(unix_time)) {
        return;
    }
    s_unix = unix_time;
    struct timeval tv = {.tv_sec = (time_t)unix_time, .tv_usec = 0};
    settimeofday(&tv, NULL);
    time_nvs_save(unix_time);
}

uint32_t vk_usb_unix_time(void)
{
    time_t now = time(NULL);
    if (now > 0 && vk_unix_is_valid((uint32_t)now)) {
        return (uint32_t)now;
    }
    return s_unix;
}

void vk_usb_push_backlight(void)
{
    if (!rx_radio_has_peer()) {
        return;
    }
    bl_send(s_bl_duty);
}

void vk_usb_hh_update(uint8_t battery, uint8_t flags)
{
    s_hh_battery = battery;
    s_hh_flags = flags;
    s_hh_last_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    s_hh_seen = true;
}

void vk_usb_hh_tick(uint32_t now_ms)
{
    if (!s_hh_seen) {
        return;
    }
    if ((now_ms - s_hh_last_ms) > VK_HB_TIMEOUT_MS) {
        s_hh_seen = false;
    }
}

bool vk_usb_hh_linked(void)
{
    return s_hh_seen && rx_radio_has_peer();
}

void vk_usb_push_time(void)
{
    if (!rx_radio_has_peer()) {
        return;
    }
    uint32_t now = vk_usb_unix_time();
    if (!vk_unix_is_valid(now)) {
        return;
    }
    vk_heartbeat_t hb = {
        .battery = s_hh_battery,
        .flags = s_hh_flags,
        .unix_time = now,
    };
    if (rx_radio_send(VK_PKT_HEARTBEAT, &hb, sizeof(hb))) {
        ESP_LOGI(TAG, "push time %lu → handheld", (unsigned long)now);
    }
}

static bool hh_linked(void)
{
    return vk_usb_hh_linked();
}

static void cdc_print_status(void)
{
    /* S=linked,bat,flags,unix  e.g. S=1,87,01,1710000000 */
    vk_usb_cdc_printf("S=%u,%u,%02X,%lu\r\n",
                      hh_linked() ? 1u : 0u,
                      (unsigned)s_hh_battery,
                      (unsigned)s_hh_flags,
                      (unsigned long)vk_usb_unix_time());
}

void vk_usb_cdc_printf(const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return;
    }
    if (tud_mounted()) {
        tud_cdc_write(buf, (uint32_t)n);
        tud_cdc_write_flush();
    }
}

bool vk_usb_hid_ready(void)
{
    return tud_mounted() && tud_hid_ready();
}

bool vk_usb_try_report(uint8_t modifiers, uint8_t keycode)
{
    if (!s_hid_mu) {
        return false;
    }
    if (xSemaphoreTake(s_hid_mu, 0) != pdTRUE) {
        return false;
    }
    bool ok = false;
    if (vk_usb_hid_ready()) {
        uint8_t report[8] = {modifiers, 0, keycode, 0, 0, 0, 0, 0};
        ok = tud_hid_n_report(0, 0, report, sizeof(report));
    }
    xSemaphoreGive(s_hid_mu);
    return ok;
}

void vk_usb_send_report(const vk_hid_report_t *r)
{
    if (!r) {
        return;
    }
    /* Pulse is unused by the keymap; still release afterwards so a modifier cannot stick. */
    if (r->pulse) {
        for (int i = 0; i < 8 && !vk_usb_try_report(r->modifiers, r->keycode); i++) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        vTaskDelay(pdMS_TO_TICKS(12));
    }
    for (int i = 0; i < 8 && !vk_usb_try_report(r->pulse ? 0 : r->modifiers, r->pulse ? 0 : r->keycode); i++) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static void keymap_nvs_save(const vk_hid_map_t *map)
{
    uint8_t blob[6] = {
        map->voice.modifiers, map->voice.keycode,
        map->yes.modifiers, map->yes.keycode,
        map->no.modifiers, map->no.keycode,
    };
    nvs_handle_t h;
    if (nvs_open("vk", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_blob(h, "keymap", blob, sizeof(blob));
    nvs_commit(h);
    nvs_close(h);
}

static bool keymap_nvs_load(vk_hid_map_t *map)
{
    uint8_t blob[6];
    size_t len = sizeof(blob);
    nvs_handle_t h;
    if (nvs_open("vk", NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_get_blob(h, "keymap", blob, &len);
    nvs_close(h);
    if (err != ESP_OK || len != sizeof(blob)) {
        return false;
    }
    map->voice.modifiers = blob[0];
    map->voice.keycode = blob[1];
    map->yes.modifiers = blob[2];
    map->yes.keycode = blob[3];
    map->no.modifiers = blob[4];
    map->no.keycode = blob[5];
    return true;
}

static void keymap_apply_default(void)
{
    vk_hid_map_t map;
    vk_hid_map_default(&map);
    vk_hid_map_set(&map);
}

static void cdc_print_keymap(void)
{
    const vk_hid_map_t *m = vk_hid_map_get();
    vk_usb_cdc_printf("K=%02X:%02X,%02X:%02X,%02X:%02X\r\n",
                      m->voice.modifiers, m->voice.keycode,
                      m->yes.modifiers, m->yes.keycode,
                      m->no.modifiers, m->no.keycode);
}

static int parse_binding(const char *s, vk_hid_binding_t *b)
{
    /* MM:CC */
    unsigned mod = 0, code = 0;
    if (sscanf(s, "%2x:%2x", &mod, &code) != 2) {
        return -1;
    }
    b->modifiers = (uint8_t)mod;
    b->keycode = (uint8_t)code;
    return 0;
}

static void parse_keymap_cmd(char *line)
{
    /* line points at after 'K'/'k' */
    while (*line == ' ' || *line == '\t' || *line == '=') {
        line++;
    }
    if (*line == '?' || *line == 0) {
        cdc_print_keymap();
        return;
    }
    if (*line == '!') {
        keymap_apply_default();
        keymap_nvs_save(vk_hid_map_get());
        vk_usb_cdc_printf("K ok\r\n");
        cdc_print_keymap();
        return;
    }

    char *p1 = line;
    char *c1 = strchr(p1, ',');
    if (!c1) {
        vk_usb_cdc_printf("K err\r\n");
        return;
    }
    *c1 = 0;
    char *p2 = c1 + 1;
    char *c2 = strchr(p2, ',');
    if (!c2) {
        vk_usb_cdc_printf("K err\r\n");
        return;
    }
    *c2 = 0;
    char *p3 = c2 + 1;

    vk_hid_map_t map;
    if (parse_binding(p1, &map.voice) || parse_binding(p2, &map.yes) || parse_binding(p3, &map.no)) {
        vk_usb_cdc_printf("K err\r\n");
        return;
    }
    vk_hid_map_set(&map);
    keymap_nvs_save(&map);
    vk_usb_cdc_printf("K ok\r\n");
    cdc_print_keymap();
}

static void parse_backlight_cmd(char *line)
{
    while (*line == ' ' || *line == '\t' || *line == '=') {
        line++;
    }
    if (*line == '?' || *line == 0) {
        vk_usb_cdc_printf("B=%u\r\n", (unsigned)s_bl_duty);
        return;
    }

    char *end = NULL;
    unsigned long duty = strtoul(line, &end, 10);
    if (end == line || duty > 255ul) {
        vk_usb_cdc_printf("B err\r\n");
        return;
    }
    s_bl_duty = (uint8_t)duty;
    bl_nvs_save(s_bl_duty);
    bool sent = bl_send(s_bl_duty);
    vk_usb_cdc_printf("B ok%s\r\n", sent ? "" : " (no peer)");
    vk_usb_cdc_printf("B=%u\r\n", (unsigned)s_bl_duty);
}

static void parse_cdc_line(char *line)
{
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (line[0] == 'T' || line[0] == 't') {
        char *rest = line + 1;
        while (*rest == ' ' || *rest == '\t' || *rest == '=') {
            rest++;
        }
        if (*rest == '?' || *rest == 0) {
            vk_usb_cdc_printf("T=%lu\r\n", (unsigned long)vk_usb_unix_time());
            return;
        }
        char *end = NULL;
        unsigned long unix_time = strtoul(rest, &end, 10);
        if (end != rest && unix_time > 1000000000ul) {
            vk_usb_set_unix_time((uint32_t)unix_time);
            vk_usb_push_time();
            vk_usb_cdc_printf("time set %lu\r\n", unix_time);
            ESP_LOGI(TAG, "time set %lu", unix_time);
        } else {
            vk_usb_cdc_printf("T err\r\n");
        }
        return;
    }
    if (line[0] == 'K' || line[0] == 'k') {
        parse_keymap_cmd(line + 1);
        return;
    }
    if (line[0] == 'B' || line[0] == 'b') {
        parse_backlight_cmd(line + 1);
        return;
    }
    if (line[0] == 'I' || line[0] == 'i') {
        parse_idle_cmd(line + 1);
        return;
    }
    if (line[0] == 'O' || line[0] == 'o') {
        parse_osd_cmd(line + 1);
        return;
    }
    if (line[0] == 'S' || line[0] == 's') {
        char *rest = line + 1;
        while (*rest == ' ' || *rest == '\t' || *rest == '=') {
            rest++;
        }
        if (*rest == '?' || *rest == 0) {
            cdc_print_status();
        }
        return;
    }
    if (line[0] == 'P' || line[0] == 'p') {
        char *rest = line + 1;
        while (*rest == ' ' || *rest == '\t' || *rest == '=') {
            rest++;
        }
        if (*rest == '?') {
            if (!rx_radio_has_peer()) {
                vk_usb_cdc_printf("P=none open=%u\r\n", rx_radio_pairing_open() ? 1u : 0u);
            } else {
                uint8_t mac[6];
                rx_radio_get_peer(mac);
                vk_usb_cdc_printf("P=%02X:%02X:%02X:%02X:%02X:%02X open=%u\r\n",
                                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                                  rx_radio_pairing_open() ? 1u : 0u);
            }
            return;
        }
        if (*rest == '!') {
            rx_radio_clear_peer();
            vk_usb_cdc_printf("P cleared — pairing open\r\n");
            return;
        }
        vk_usb_cdc_printf("P? peer; P! clear peer\r\n");
    }
}

void tud_cdc_rx_cb(uint8_t itf)
{
    (void)itf;
    static char line[64];
    static size_t len;
    while (tud_cdc_available()) {
        char c = (char)tud_cdc_read_char();
        if (c == '\r' || c == '\n') {
            if (len) {
                line[len] = 0;
                parse_cdc_line(line);
                len = 0;
            }
        } else if (len + 1 < sizeof(line)) {
            line[len++] = c;
        } else {
            len = 0;
        }
    }
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

bool tud_audio_tx_done_pre_load_cb(uint8_t rhport, uint8_t itf, uint8_t ep_in, uint8_t cur_alt_setting)
{
    (void)rhport;
    (void)itf;
    (void)ep_in;
    (void)cur_alt_setting;

    size_t samples = sizeof(s_audio_buf) / sizeof(s_audio_buf[0]);
    if (audio_ring_is_live()) {
        audio_ring_read(s_audio_buf, samples);
    } else {
        memset(s_audio_buf, 0, sizeof(s_audio_buf));
    }
    if (s_mute[0] || s_mute[1]) {
        memset(s_audio_buf, 0, sizeof(s_audio_buf));
    }
    tud_audio_write((uint8_t *)s_audio_buf, sizeof(s_audio_buf));
    return true;
}

bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff)
{
    (void)rhport;
    uint8_t channelNum = TU_U16_LOW(p_request->wValue);
    uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
    uint8_t entityID = TU_U16_HIGH(p_request->wIndex);

    TU_VERIFY(p_request->bRequest == AUDIO_CS_REQ_CUR);
    if (entityID == 2) {
        switch (ctrlSel) {
        case AUDIO_FU_CTRL_MUTE:
            TU_VERIFY(p_request->wLength == sizeof(audio_control_cur_1_t));
            s_mute[channelNum] = ((audio_control_cur_1_t *)pBuff)->bCur;
            return true;
        case AUDIO_FU_CTRL_VOLUME:
            TU_VERIFY(p_request->wLength == sizeof(audio_control_cur_2_t));
            s_volume[channelNum] = (uint16_t)((audio_control_cur_2_t *)pBuff)->bCur;
            return true;
        default:
            return false;
        }
    }
    return false;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    uint8_t channelNum = TU_U16_LOW(p_request->wValue);
    uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
    uint8_t entityID = TU_U16_HIGH(p_request->wIndex);

    if (entityID == 1 && ctrlSel == AUDIO_TE_CTRL_CONNECTOR) {
        audio_desc_channel_cluster_t ret = {
            .bNrChannels = 1,
            .bmChannelConfig = 0,
            .iChannelNames = 0,
        };
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &ret, sizeof(ret));
    }

    if (entityID == 2) {
        if (ctrlSel == AUDIO_FU_CTRL_MUTE) {
            return tud_control_xfer(rhport, p_request, &s_mute[channelNum], 1);
        }
        if (ctrlSel == AUDIO_FU_CTRL_VOLUME) {
            if (p_request->bRequest == AUDIO_CS_REQ_CUR) {
                return tud_control_xfer(rhport, p_request, &s_volume[channelNum], sizeof(s_volume[channelNum]));
            }
            if (p_request->bRequest == AUDIO_CS_REQ_RANGE) {
                audio_control_range_2_n_t(1) ret;
                ret.wNumSubRanges = 1;
                ret.subrange[0].bMin = -90;
                ret.subrange[0].bMax = 90;
                ret.subrange[0].bRes = 1;
                return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &ret, sizeof(ret));
            }
        }
    }

    if (entityID == 4) {
        if (ctrlSel == AUDIO_CS_CTRL_SAM_FREQ) {
            if (p_request->bRequest == AUDIO_CS_REQ_CUR) {
                return tud_control_xfer(rhport, p_request, &s_samp_freq, sizeof(s_samp_freq));
            }
            if (p_request->bRequest == AUDIO_CS_REQ_RANGE) {
                return tud_control_xfer(rhport, p_request, &s_sample_freq_rng, sizeof(s_sample_freq_rng));
            }
        }
        if (ctrlSel == AUDIO_CS_CTRL_CLK_VALID) {
            return tud_control_xfer(rhport, p_request, &s_clk_valid, sizeof(s_clk_valid));
        }
    }
    return false;
}

bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff)
{
    (void)rhport;
    (void)p_request;
    (void)pBuff;
    return false;
}

bool tud_audio_set_req_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff)
{
    (void)rhport;
    (void)p_request;
    (void)pBuff;
    return false;
}

bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    (void)rhport;
    (void)p_request;
    return false;
}

bool tud_audio_get_req_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
    (void)rhport;
    (void)p_request;
    return false;
}

void vk_usb_init(void)
{
    s_hid_mu = xSemaphoreCreateMutex();
    audio_ring_init();
    phy_init();

    s_sample_freq_rng.wNumSubRanges = 1;
    s_sample_freq_rng.subrange[0].bMin = CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;
    s_sample_freq_rng.subrange[0].bMax = CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;
    s_sample_freq_rng.subrange[0].bRes = 0;
    s_volume[0] = 0;
    s_volume[1] = 0;

    uint32_t saved = 0;
    if (time_nvs_load(&saved)) {
        s_unix = saved;
        struct timeval tv = {.tv_sec = (time_t)saved, .tv_usec = 0};
        settimeofday(&tv, NULL);
        ESP_LOGI(TAG, "restored time %lu", (unsigned long)saved);
    }

    vk_hid_map_t kmap;
    if (keymap_nvs_load(&kmap)) {
        vk_hid_map_set(&kmap);
        ESP_LOGI(TAG, "restored keymap");
    } else {
        keymap_apply_default();
    }

    bl_nvs_load();
    idle_nvs_load();
    ESP_LOGI(TAG, "backlight duty %u idle_blank %u", (unsigned)s_bl_duty, (unsigned)s_idle_blank);

    if (!tusb_init()) {
        ESP_LOGE(TAG, "tusb_init failed");
        return;
    }
    xTaskCreatePinnedToCore(usb_task, "tusb", 4096, NULL, 5, NULL, 0);
    ESP_LOGI(TAG, "Zhiyan Receiver USB up");
}
