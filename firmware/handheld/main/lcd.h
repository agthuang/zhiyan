#pragma once

#include "vk_ui.h"

#include <stdint.h>

void lcd_init(void);
void lcd_set_backlight(uint8_t duty);
void lcd_flush(const uint16_t *fb);
void lcd_show(const vk_ui_model_t *m);
