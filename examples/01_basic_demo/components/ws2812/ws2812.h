/*
 * WS2812 RGB LED 驱动头文件
 * RMT 外设直接驱动, GPIO37, 单颗 LED
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t ws2812_init(void);
esp_err_t ws2812_set_color_rgb(uint8_t r, uint8_t g, uint8_t b);
esp_err_t ws2812_breath_start(void);
