#pragma once

#include <stdbool.h>
#include <stdint.h>

void status_led_init(void);

/** Update link/voice indication. Safe to call every loop (~20 ms). */
void status_led_tick(uint32_t now_ms, bool linked, bool voice_held);
