#include "vk_battery.h"

int vk_battery_from_sense_mv(int vsense_mv)
{
    if (vsense_mv < 0) {
        vsense_mv = 0;
    }
    /* VBAT -- 1M -- sense -- 330k -- GND */
    return (int)(((int32_t)vsense_mv * 1330) / 330);
}

uint8_t vk_battery_percent(int vbat_mv)
{
    const int empty = 3300;
    const int full = 4150;
    if (vbat_mv <= empty) {
        return 0;
    }
    if (vbat_mv >= full) {
        return 100;
    }
    return (uint8_t)(((vbat_mv - empty) * 100) / (full - empty));
}
