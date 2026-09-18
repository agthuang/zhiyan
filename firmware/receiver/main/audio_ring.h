#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

void audio_ring_init(void);
void audio_ring_reset(void);
size_t audio_ring_write(const int16_t *samples, size_t count);
size_t audio_ring_read(int16_t *samples, size_t count);
void audio_ring_set_live(bool live);
bool audio_ring_is_live(void);
