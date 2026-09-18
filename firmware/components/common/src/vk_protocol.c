#include "vk_protocol.h"

#include <string.h>

size_t vk_pack(uint8_t *out, size_t cap, vk_pkt_type_t type, uint16_t seq,
               const void *payload, uint8_t len)
{
    if (out == NULL || cap < (size_t)(VK_HDR_SIZE + len)) {
        return 0;
    }
    if (payload == NULL && len != 0) {
        return 0;
    }

    vk_hdr_t hdr;
    hdr.magic = VK_MAGIC;
    hdr.ver = VK_PROTO_VER;
    hdr.type = (uint8_t)type;
    hdr.seq = seq;
    hdr.len = len;
    hdr.flags = 0;
    memcpy(out, &hdr, sizeof(hdr));
    if (len != 0) {
        memcpy(out + VK_HDR_SIZE, payload, len);
    }
    return (size_t)VK_HDR_SIZE + len;
}

bool vk_parse(const uint8_t *in, size_t n, vk_hdr_t *hdr, const uint8_t **payload)
{
    if (in == NULL || hdr == NULL || n < VK_HDR_SIZE) {
        return false;
    }
    memcpy(hdr, in, sizeof(*hdr));
    if (hdr->magic != VK_MAGIC || hdr->ver != VK_PROTO_VER) {
        return false;
    }
    if ((size_t)VK_HDR_SIZE + hdr->len > n) {
        return false;
    }
    if (payload != NULL) {
        *payload = (hdr->len == 0) ? NULL : (in + VK_HDR_SIZE);
    }
    return true;
}

bool vk_unix_is_valid(uint32_t unix_time)
{
    /* 2020-01-01 is 1577836800; anything earlier is "not wall time". */
    return unix_time >= 1577836800u;
}

void vk_unix_to_hm(uint32_t unix_time, int16_t tz_min, uint8_t *hour, uint8_t *minute)
{
    int64_t local = (int64_t)unix_time + (int64_t)tz_min * 60;
    if (local < 0) {
        local = 0;
    }
    uint32_t tod = (uint32_t)(local % 86400);
    if (hour) {
        *hour = (uint8_t)(tod / 3600);
    }
    if (minute) {
        *minute = (uint8_t)((tod % 3600) / 60);
    }
}

uint16_t vk_rgb888_to_565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)((((uint16_t)r & 0xF8u) << 8) | (((uint16_t)g & 0xFCu) << 3) | ((uint16_t)b >> 3));
}

static int osd_ok_char(char c)
{
    if (c >= 'a' && c <= 'z') {
        return 1;
    }
    if (c >= 'A' && c <= 'Z') {
        return 1;
    }
    if (c >= '0' && c <= '9') {
        return 1;
    }
    return c == '?' || c == '!' || c == '.' || c == '_' || c == '-';
}

uint8_t vk_osd_sanitize(char *dst, size_t dst_cap, const char *src)
{
    if (dst == NULL || dst_cap == 0) {
        return 0;
    }
    size_t max = dst_cap - 1;
    if (max > VK_OSD_TEXT_MAX) {
        max = VK_OSD_TEXT_MAX;
    }
    uint8_t n = 0;
    if (src) {
        for (; *src && n < max; src++) {
            char c = *src;
            if (!osd_ok_char(c)) {
                continue;
            }
            if (c >= 'a' && c <= 'z') {
                c = (char)(c - 'a' + 'A');
            }
            dst[n++] = c;
        }
    }
    dst[n] = 0;
    return n;
}
