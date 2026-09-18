#pragma once

#include "vk_hid.h"

#include <stdbool.h>
#include <stdint.h>

void vk_usb_init(void);
bool vk_usb_hid_ready(void);
void vk_usb_send_report(const vk_hid_report_t *r);
void vk_usb_cdc_printf(const char *fmt, ...);
uint32_t vk_usb_unix_time(void);
void vk_usb_set_unix_time(uint32_t unix_time);
/** Re-send last backlight duty to the paired handheld (no-op if unpaired). */
void vk_usb_push_backlight(void);
/** Cache handheld telemetry from ESP-NOW heartbeat/status. */
void vk_usb_hh_update(uint8_t battery, uint8_t flags);
void vk_usb_hh_tick(uint32_t now_ms);
/** Immediately push wall clock to paired handheld via heartbeat. */
void vk_usb_push_time(void);
