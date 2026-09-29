#pragma once

#include "vk_hid.h"

#include <stdbool.h>
#include <stdint.h>

void vk_usb_init(void);
bool vk_usb_hid_ready(void);
/** One attempt. False if the host has not accepted the report yet — caller must retry.
 *  A dropped all-zero report leaves modifiers stuck until USB unplug. */
bool vk_usb_try_report(uint8_t modifiers, uint8_t keycode);
void vk_usb_send_report(const vk_hid_report_t *r);
void vk_usb_cdc_printf(const char *fmt, ...);
uint32_t vk_usb_unix_time(void);
void vk_usb_set_unix_time(uint32_t unix_time);
/** Re-send last backlight duty to the paired handheld (no-op if unpaired). */
void vk_usb_push_backlight(void);
/** Re-send idle-blank flag to the paired handheld (no-op if unpaired). */
void vk_usb_push_idle(void);
/** Re-send RF eco flag to the paired handheld (no-op if unpaired). */
void vk_usb_push_eco(void);
/** Cache handheld telemetry from ESP-NOW heartbeat/status. */
void vk_usb_hh_update(uint8_t battery, uint8_t flags);
void vk_usb_hh_tick(uint32_t now_ms);
/** True while a handheld has spoken recently and a peer is bound. */
bool vk_usb_hh_linked(void);
/** Immediately push wall clock to paired handheld via heartbeat. */
void vk_usb_push_time(void);
