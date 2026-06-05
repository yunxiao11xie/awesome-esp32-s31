/*
 * ST7262E43 RGB LCD 驱动头文件
 * 4.3 inch, 800x480, RGB565
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#define LCD_H_RES   800
#define LCD_V_RES   480

#define LCD_COLOR_BLACK      0x0000
#define LCD_COLOR_WHITE      0xFFFF
#define LCD_COLOR_RED        0xF800
#define LCD_COLOR_GREEN      0x07E0
#define LCD_COLOR_BLUE       0x001F
#define LCD_COLOR_CYAN       0x07FF
#define LCD_COLOR_YELLOW     0xFFE0
#define LCD_COLOR_MAGENTA    0xF81F

esp_err_t lcd_st7262_init(void);
uint16_t* lcd_get_frame_buffer(void);
void lcd_get_frame_buffers(uint16_t **fb0_out, uint16_t **fb1_out);
esp_lcd_panel_handle_t lcd_get_panel(void);
esp_err_t lcd_refresh(void);
void lcd_draw_string(uint16_t *fb, int x, int y, const char *str,
                     uint16_t fg_color, uint16_t bg_color);
void lcd_clear(uint16_t *fb, uint16_t color);
uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b);
