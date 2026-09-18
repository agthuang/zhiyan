#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VK_MAGIC            0x4B56u /* 'VK' */
#define VK_PROTO_VER        1u
#define VK_ESPNOW_CHANNEL   1
#define VK_ESPNOW_MAX       250
#define VK_HDR_SIZE         8
#define VK_AUDIO_SAMPLES    120
#define VK_AUDIO_BYTES      (VK_AUDIO_SAMPLES * 2)
#define VK_SAMPLE_HZ        16000
#define VK_TZ_DEFAULT_MIN   480 /* UTC+8 */
#define VK_HB_MS            400
#define VK_HB_TIMEOUT_MS    1600

typedef enum {
    VK_PKT_PAIR_REQ   = 1,
    VK_PKT_PAIR_ACK   = 2,
    VK_PKT_KEY        = 3,
    VK_PKT_AUDIO      = 4,
    VK_PKT_STATUS     = 5,
    VK_PKT_HEARTBEAT  = 6,
    VK_PKT_BACKLIGHT  = 7, /* rx → handheld: PWM duty 0–255 */
    VK_PKT_OSD        = 8, /* rx → handheld: colored English word / clear */
    VK_PKT_IDLE_BLANK = 9, /* rx → handheld: idle backlight blank enable */
} vk_pkt_type_t;

#define VK_BL_DEFAULT   64u /* ≈25% — matches board.h soft backlight */
#define VK_OSD_TEXT_MAX 8
#define VK_OSD_FLAG_CLEAR (1u << 0)

typedef enum {
    VK_KEY_VOICE = 0,
    VK_KEY_YES   = 1,
    VK_KEY_NO    = 2,
} vk_key_id_t;

typedef enum {
    VK_ACT_UP   = 0,
    VK_ACT_DOWN = 1,
} vk_act_t;

#define VK_STAT_CHARGING  (1u << 0)
#define VK_STAT_CHG_DONE  (1u << 1)
#define VK_STAT_TALKING   (1u << 2)

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t  ver;
    uint8_t  type;
    uint16_t seq;
    uint8_t  len;
    uint8_t  flags;
} vk_hdr_t;

typedef struct __attribute__((packed)) {
    uint8_t mac[6];
    uint8_t ver;
    uint8_t _pad;
} vk_pair_req_t;

typedef struct __attribute__((packed)) {
    uint8_t  mac[6];
    uint8_t  ver;
    uint8_t  _pad;
    uint32_t unix_time;
} vk_pair_ack_t;

typedef struct __attribute__((packed)) {
    uint8_t key;
    uint8_t action;
} vk_key_t;

typedef struct __attribute__((packed)) {
    uint8_t  battery;
    uint8_t  flags;
    uint16_t _pad;
    uint32_t unix_time;
} vk_heartbeat_t;

typedef struct __attribute__((packed)) {
    uint8_t battery;
    uint8_t flags;
} vk_status_t;

typedef struct __attribute__((packed)) {
    uint8_t duty; /* LEDC 8-bit: 0 = off, 255 = full */
} vk_backlight_t;

typedef struct __attribute__((packed)) {
    uint16_t color; /* RGB565 */
    uint8_t  flags; /* VK_OSD_FLAG_CLEAR */
    uint8_t  len;   /* bytes used in text[0..len) */
    char     text[VK_OSD_TEXT_MAX];
} vk_osd_t;

typedef struct __attribute__((packed)) {
    uint8_t enabled; /* 0 = off (always lit), 1 = blank after idle */
    uint8_t _pad;
    uint16_t timeout_sec; /* 0 = default (20s); reserved for Phase 1B */
} vk_idle_blank_t;

size_t vk_pack(uint8_t *out, size_t cap, vk_pkt_type_t type, uint16_t seq,
               const void *payload, uint8_t len);

bool vk_parse(const uint8_t *in, size_t n, vk_hdr_t *hdr, const uint8_t **payload);

bool vk_unix_is_valid(uint32_t unix_time);
void vk_unix_to_hm(uint32_t unix_time, int16_t tz_min, uint8_t *hour, uint8_t *minute);

uint16_t vk_rgb888_to_565(uint8_t r, uint8_t g, uint8_t b);
/** Keep A–Z a–z 0–9 ?!._- ; uppercase; truncate to VK_OSD_TEXT_MAX. Returns len. */
uint8_t vk_osd_sanitize(char *dst, size_t dst_cap, const char *src);
