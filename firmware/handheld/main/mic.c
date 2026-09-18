#include "mic.h"

#include "board.h"
#include "vk_protocol.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "mic";

/* Keep modest — loud gain turns MEMS floor into 滋滋 noise. */
#define MIC_GAIN 2
/* Soft limiter: close-talk overload clips harshly into 滋滋. */
#define LIM_THRESH 18000
/* Envelope gate: mute trailing hiss after speech ends. */
#define GATE_OPEN  480
#define GATE_CLOSE 160
/* One-pole DC blocker / HPF (~120 Hz @ 16 kHz). */
#define HPF_R_Q15 31232 /* 0.953 * 32768 */

static i2s_chan_handle_t s_rx;
static mic_frame_cb_t s_cb;
static void *s_ctx;
static volatile bool s_want;
static volatile bool s_running;
static int32_t s_hpf_x1;
static int32_t s_hpf_y1;
static int32_t s_env;

static void mic_power(bool on)
{
    gpio_set_level(PIN_MIC_PWR_EN, on ? 0 : 1); /* active low */
}

static void dsp_reset(void)
{
    s_hpf_x1 = 0;
    s_hpf_y1 = 0;
    s_env = 0;
}

static int16_t process_sample(int32_t raw)
{
    /* ICS-43434: 24-bit left-justified in 32-bit Philips slot. */
    int32_t x = raw >> 16;
    x *= MIC_GAIN;

    /* y = x - x1 + R*y1 */
    int32_t y = x - s_hpf_x1 + (int32_t)((s_hpf_y1 * (int64_t)HPF_R_Q15) >> 15);
    s_hpf_x1 = x;
    s_hpf_y1 = y;

    int32_t a = y < 0 ? -y : y;
    /* Fast attack, ~40 ms release @ 16 kHz — closes on post-speech hush. */
    if (a > s_env) {
        s_env = a;
    } else {
        s_env -= (s_env >> 8);
    }

    if (s_env < GATE_CLOSE) {
        y = 0;
    } else if (s_env < GATE_OPEN) {
        y = (y * (s_env - GATE_CLOSE)) / (GATE_OPEN - GATE_CLOSE);
    }

    /* Soft knee: fold peaks instead of hard clipping when mic is close. */
    int32_t ay = y < 0 ? -y : y;
    if (ay > LIM_THRESH) {
        ay = LIM_THRESH + ((ay - LIM_THRESH) >> 2);
        if (ay > 32767) {
            ay = 32767;
        }
        y = y < 0 ? -ay : ay;
    }
    return (int16_t)y;
}

static void mic_task(void *arg)
{
    (void)arg;
    int32_t raw[VK_AUDIO_SAMPLES * 2];
    int16_t pcm[VK_AUDIO_SAMPLES];

    while (true) {
        if (!s_want) {
            if (s_running) {
                i2s_channel_disable(s_rx);
                mic_power(false);
                s_running = false;
                dsp_reset();
                ESP_LOGI(TAG, "stopped");
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (!s_running) {
            mic_power(true);
            vTaskDelay(pdMS_TO_TICKS(80));
            dsp_reset();
            ESP_ERROR_CHECK(i2s_channel_enable(s_rx));
            /* Discard a few frames while the mic bias settles. */
            size_t discard = 0;
            for (int i = 0; i < 4; i++) {
                i2s_channel_read(s_rx, raw, sizeof(raw), &discard, pdMS_TO_TICKS(50));
            }
            s_running = true;
            ESP_LOGI(TAG, "streaming");
        }

        size_t bytes = 0;
        esp_err_t err = i2s_channel_read(s_rx, raw, sizeof(raw), &bytes, pdMS_TO_TICKS(100));
        if (err != ESP_OK || bytes < sizeof(int32_t) * 2) {
            continue;
        }
        size_t frames = bytes / (sizeof(int32_t) * 2);
        if (frames > VK_AUDIO_SAMPLES) {
            frames = VK_AUDIO_SAMPLES;
        }
        for (size_t i = 0; i < frames; i++) {
            pcm[i] = process_sample(raw[i * 2]);
        }
        if (s_cb) {
            s_cb(pcm, frames, s_ctx);
        }
    }
}

void mic_init(mic_frame_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_ctx = ctx;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_MIC_PWR_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    mic_power(false);
    dsp_reset();

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan, NULL, &s_rx));

    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(VK_SAMPLE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S_BCLK,
            .ws = PIN_I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = PIN_I2S_SD,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx, &std));

    xTaskCreate(mic_task, "mic", 4096, NULL, 6, NULL);
    ESP_LOGI(TAG, "init %d Hz gain=%d gate=%d/%d", VK_SAMPLE_HZ, MIC_GAIN, GATE_CLOSE, GATE_OPEN);
}

void mic_set_streaming(bool on)
{
    s_want = on;
}

bool mic_is_streaming(void)
{
    return s_running;
}
