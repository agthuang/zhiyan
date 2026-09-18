#pragma once

#include <stdint.h>

/* LiPo helper: millivolts at the cell, after undoing the 1M / 330k divider. */
uint8_t vk_battery_percent(int vbat_mv);
int vk_battery_from_sense_mv(int vsense_mv);
