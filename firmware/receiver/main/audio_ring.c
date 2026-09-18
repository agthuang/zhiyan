#include "audio_ring.h"

#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

#include <string.h>

#define AUDIO_RING_BYTES (3200 * sizeof(int16_t)) /* 200 ms @ 16 kHz */

static StreamBufferHandle_t s_sb;
static volatile bool s_live;

void audio_ring_init(void)
{
    s_sb = xStreamBufferCreate(AUDIO_RING_BYTES, 1);
    s_live = false;
}

void audio_ring_reset(void)
{
    if (s_sb) {
        xStreamBufferReset(s_sb);
    }
}

void audio_ring_set_live(bool live)
{
    s_live = live;
    if (!live) {
        audio_ring_reset();
    }
}

bool audio_ring_is_live(void)
{
    return s_live;
}

size_t audio_ring_write(const int16_t *samples, size_t count)
{
    if (!s_sb || !samples || count == 0) {
        return 0;
    }
    size_t n = xStreamBufferSend(s_sb, samples, count * sizeof(int16_t), 0);
    return n / sizeof(int16_t);
}

size_t audio_ring_read(int16_t *samples, size_t count)
{
    if (!s_sb || !samples || count == 0) {
        return 0;
    }
    size_t n = xStreamBufferReceive(s_sb, samples, count * sizeof(int16_t), 0);
    size_t got = n / sizeof(int16_t);
    if (got < count) {
        memset(samples + got, 0, (count - got) * sizeof(int16_t));
    }
    return got;
}
