#include "battery_hw.h"

#include "board.h"
#include "vk_battery.h"

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "bat";

/* LiPo under load while the board is alive should never look like <2.5 V. */
#define VBAT_PLAUSIBLE_MIN_MV 2500

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_cali_ok;
static adc_channel_t s_chan;
static adc_unit_t s_unit;

void battery_hw_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_CHG_DONE_N) | (1ULL << PIN_CHG_STAT_N),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    /* GPIO3 is an S3 strapping pin — reset so leftover pulls don't load the divider. */
    gpio_reset_pin(PIN_BAT_SENSE);
    gpio_set_direction(PIN_BAT_SENSE, GPIO_MODE_DISABLE);
    gpio_set_pull_mode(PIN_BAT_SENSE, GPIO_FLOATING);

    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(PIN_BAT_SENSE, &s_unit, &s_chan));

    adc_oneshot_unit_init_cfg_t unit = {
        .unit_id = s_unit,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &s_adc));

    adc_oneshot_chan_cfg_t ch = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, s_chan, &ch));

    adc_cali_curve_fitting_config_t cali = {
        .unit_id = s_unit,
        .chan = s_chan,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali, &s_cali) == ESP_OK) {
        s_cali_ok = true;
    } else {
        ESP_LOGW(TAG, "ADC calibration unavailable");
    }
    ESP_LOGI(TAG, "sense GPIO%d ADC%d CH%d", (int)PIN_BAT_SENSE, (int)s_unit + 1, (int)s_chan);
}

static int read_sense_mv(int *raw_out)
{
    int sum = 0;
    int raw = 0;
    for (int i = 0; i < 8; i++) {
        int r = 0;
        if (adc_oneshot_read(s_adc, s_chan, &r) == ESP_OK) {
            sum += r;
            raw = r;
        }
    }
    raw = sum / 8;
    if (raw_out) {
        *raw_out = raw;
    }
    int vsense_mv = (raw * 3100) / 4095;
    if (s_cali_ok) {
        adc_cali_raw_to_voltage(s_cali, raw, &vsense_mv);
    }
    return vsense_mv;
}

battery_sample_t battery_hw_read(void)
{
    battery_sample_t out = {0};
    int raw = 0;
    int vsense_mv = read_sense_mv(&raw);
    out.vbat_mv = vk_battery_from_sense_mv(vsense_mv);
    out.charging = !gpio_get_level(PIN_CHG_STAT_N);
    out.done = !gpio_get_level(PIN_CHG_DONE_N);

    bool adc_ok = out.vbat_mv >= VBAT_PLAUSIBLE_MIN_MV;
    if (adc_ok) {
        out.percent = vk_battery_percent(out.vbat_mv);
    } else {
        /* Divider/GPIO3 reads ~50 mV on this board — trust charger status pins. */
        if (out.done && !out.charging) {
            out.percent = 100;
            out.vbat_mv = 4200; /* display-only stand-in */
        } else if (out.charging) {
            out.percent = 80; /* charging, level unknown */
            out.vbat_mv = 4000;
        } else {
            out.percent = 0;
        }
    }

    /* If charger says full, never show a low percentage. */
    if (out.done && !out.charging && out.percent < 95) {
        out.percent = 100;
    }

    static int s_log_n;
    if (s_log_n < 5 || (s_log_n % 30) == 0) {
        ESP_LOGI(TAG, "vbat=%dmV %u%% chg=%d done=%d raw=%d sense=%dmV adc_ok=%d",
                 out.vbat_mv, out.percent, out.charging, out.done, raw, vsense_mv, (int)adc_ok);
    }
    s_log_n++;
    return out;
}
