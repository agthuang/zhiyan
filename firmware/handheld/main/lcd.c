#include "lcd.h"

#include "board.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "lcd";

static spi_device_handle_t s_spi;
static uint16_t s_fb[VK_LCD_W * VK_LCD_H];
static vk_ui_model_t s_last;
static int s_have_last;

static void lcd_cmd(uint8_t cmd)
{
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    gpio_set_level(PIN_LCD_DC, 0);
    spi_device_polling_transmit(s_spi, &t);
}

static void lcd_data(const void *data, size_t len)
{
    if (len == 0) {
        return;
    }
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    gpio_set_level(PIN_LCD_DC, 1);
    spi_device_polling_transmit(s_spi, &t);
}

static void lcd_data8(uint8_t v)
{
    lcd_data(&v, 1);
}

static void lcd_data16(uint16_t v)
{
    uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v};
    lcd_data(b, 2);
}

static void lcd_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    x0 = (uint16_t)(x0 + LCD_X_GAP);
    x1 = (uint16_t)(x1 + LCD_X_GAP);
    y0 = (uint16_t)(y0 + LCD_Y_GAP);
    y1 = (uint16_t)(y1 + LCD_Y_GAP);
    lcd_cmd(0x2A);
    lcd_data16(x0);
    lcd_data16(x1);
    lcd_cmd(0x2B);
    lcd_data16(y0);
    lcd_data16(y1);
    lcd_cmd(0x2C);
}

void lcd_set_backlight(uint8_t duty)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void lcd_init(void)
{
    gpio_config_t pwr = {
        .pin_bit_mask = (1ULL << PIN_DISP_PWR_EN) | (1ULL << PIN_LCD_DC) | (1ULL << PIN_LCD_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pwr);
    gpio_set_level(PIN_DISP_PWR_EN, 0); /* enable */
    gpio_set_level(PIN_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = VK_LCD_W * VK_LCD_H * 2 + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {
        .clock_speed_hz = 26 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_LCD_CS,
        .queue_size = 1,
        .flags = SPI_DEVICE_NO_DUMMY,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &s_spi));

    ledc_timer_config_t tmr = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&tmr);
    ledc_channel_config_t ch = {
        .gpio_num = PIN_LCD_BLK,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&ch);

    /* ST7735S mini 80x160 init (Adafruit-style). */
    lcd_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(150));
    lcd_cmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(255));
    lcd_cmd(0xB1);
    lcd_data8(0x01);
    lcd_data8(0x2C);
    lcd_data8(0x2D);
    lcd_cmd(0xB2);
    lcd_data8(0x01);
    lcd_data8(0x2C);
    lcd_data8(0x2D);
    lcd_cmd(0xB3);
    lcd_data8(0x01);
    lcd_data8(0x2C);
    lcd_data8(0x2D);
    lcd_data8(0x01);
    lcd_data8(0x2C);
    lcd_data8(0x2D);
    lcd_cmd(0xB4);
    lcd_data8(0x07);
    lcd_cmd(0xC0);
    lcd_data8(0xA2);
    lcd_data8(0x02);
    lcd_data8(0x84);
    lcd_cmd(0xC1);
    lcd_data8(0xC5);
    lcd_cmd(0xC2);
    lcd_data8(0x0A);
    lcd_data8(0x00);
    lcd_cmd(0xC3);
    lcd_data8(0x8A);
    lcd_data8(0x2A);
    lcd_cmd(0xC4);
    lcd_data8(0x8A);
    lcd_data8(0xEE);
    lcd_cmd(0xC5);
    lcd_data8(0x0E);
    lcd_cmd(0x36);
    lcd_data8(LCD_MADCTL);
    lcd_cmd(0x3A);
    lcd_data8(0x05);
#if LCD_INVERT
    lcd_cmd(0x21); /* INVON — some IPS need this; others wash out if set */
#else
    lcd_cmd(0x20); /* INVOFF */
#endif
    lcd_cmd(0x13);
    lcd_cmd(0x29);
    vTaskDelay(pdMS_TO_TICKS(20));

    lcd_set_backlight(LCD_BL_DUTY);
    memset(&s_last, 0, sizeof(s_last));
    s_have_last = 0;
    ESP_LOGI(TAG, "ready %dx%d bl=%u", VK_LCD_W, VK_LCD_H, LCD_BL_DUTY);
}

void lcd_flush(const uint16_t *fb)
{
    lcd_window(0, 0, VK_LCD_W - 1, VK_LCD_H - 1);
    /* SPI wants big-endian RGB565 */
    static uint16_t line[VK_LCD_W];
    for (int y = 0; y < VK_LCD_H; y++) {
        const uint16_t *src = fb + y * VK_LCD_W;
        for (int x = 0; x < VK_LCD_W; x++) {
            uint16_t c = src[x];
            line[x] = (uint16_t)((c >> 8) | (c << 8));
        }
        lcd_data(line, sizeof(line));
    }
}

void lcd_show(const vk_ui_model_t *m)
{
    if (s_have_last && memcmp(&s_last, m, sizeof(*m)) == 0) {
        return;
    }
    vk_ui_render(s_fb, m);
    lcd_flush(s_fb);
    s_last = *m;
    s_have_last = 1;
}
