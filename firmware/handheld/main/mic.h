#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*mic_frame_cb_t)(const int16_t *samples, size_t count, void *ctx);

void mic_init(mic_frame_cb_t cb, void *ctx);
void mic_set_streaming(bool on);
bool mic_is_streaming(void);
