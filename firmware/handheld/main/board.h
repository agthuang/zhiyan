#pragma once

#include "driver/gpio.h"

/* Controller-RevB — flash 4/8/16MB via PIO env (docs/flashing.md §4.0). README §6. */

#define PIN_BTN_VOICE   GPIO_NUM_0
#define PIN_BTN_YES     GPIO_NUM_10
#define PIN_BTN_NO      GPIO_NUM_9

#define PIN_I2S_BCLK    GPIO_NUM_12
#define PIN_I2S_WS      GPIO_NUM_14
#define PIN_I2S_SD      GPIO_NUM_13
#define PIN_MIC_PWR_EN  GPIO_NUM_11 /* active low */

#define PIN_LCD_SCLK    GPIO_NUM_42
#define PIN_LCD_MOSI    GPIO_NUM_41
#define PIN_LCD_CS      GPIO_NUM_38
#define PIN_LCD_DC      GPIO_NUM_39
#define PIN_LCD_RST     GPIO_NUM_40
#define PIN_LCD_BLK     GPIO_NUM_2
#define PIN_DISP_PWR_EN GPIO_NUM_1 /* active low */

#define PIN_BAT_SENSE   GPIO_NUM_3
#define PIN_CHG_DONE_N  GPIO_NUM_47 /* active low */
#define PIN_CHG_STAT_N  GPIO_NUM_48 /* active low */

/*
 * 0.96" ST7735 80x160 → landscape 160x80.
 * 不同厂家模组 GRAM 窗口偏移 / 是否反色常不一致：
 *   0 = 现用正常屏（X=1 Y=26 + invert）
 *   1 = 另一厂家常见款（Adafruit MINI160x80：X=0 Y=24，不反色）
 * 新屏斜切花屏 / 发白时改 LCD_PANEL_VARIANT 后重烧。
 */
#ifndef LCD_PANEL_VARIANT
#define LCD_PANEL_VARIANT 0
#endif
#if LCD_PANEL_VARIANT == 0
#define LCD_X_GAP 1
#define LCD_Y_GAP 26
#define LCD_INVERT 1
#else
#define LCD_X_GAP 0
#define LCD_Y_GAP 24
#define LCD_INVERT 0
#endif
#define LCD_MADCTL 0x68 /* MV | MX | BGR — BGR so oranges aren't blue on this ST7735 */
#define LCD_BL_DUTY 64  /* /255 ≈ 25% — soft, not glaring */
