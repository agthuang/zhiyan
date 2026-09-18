#pragma once

#include <stdint.h>

typedef struct {
    uint8_t percent;
    uint8_t charging;
    uint8_t done;
    int vbat_mv;
} battery_sample_t;

void battery_hw_init(void);
battery_sample_t battery_hw_read(void);
