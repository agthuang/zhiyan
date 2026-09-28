#pragma once

#include "driver/gpio.h"

/* ESP32-S3 SuperMini: onboard WS2812 (+ shared discrete red) on GPIO48.
 * Blue BAT LED is charge-IC only — not software-controllable. */
#define PIN_STATUS_LED GPIO_NUM_48
