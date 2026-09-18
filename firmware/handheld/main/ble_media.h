#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Block forever in BLE keyboard remote: ↑ / Space / ↓. */
void ble_media_run(void);

/** True if Yes stayed held from power-on for hold_ms (flip power while holding Yes). */
bool ble_media_boot_hold_yes(uint32_t hold_ms);
