#pragma once

#include <stdbool.h>
#include <stdint.h>

void buttons_init(void);
uint8_t buttons_raw_mask(void);

/**
 * Power-on hold detector: key already down at boot for hold_ms → true.
 * If key never pressed, returns false after ~250ms (no mode splash).
 */
bool buttons_boot_hold(uint8_t mask, uint32_t hold_ms);
