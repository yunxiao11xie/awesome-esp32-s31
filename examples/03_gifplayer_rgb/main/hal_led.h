/**
 * @file hal_led.h
 * @brief WS2812 RGB LED 硬件抽象层
 *
 * 封装 LED 状态（on/off、模式、亮度）为内部 static 变量，
 * 对外仅暴露行为接口。调用方不需要知道 LED 的内部状态存储方式。
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 WS2812 RGB LED（RMT 驱动）
 */
esp_err_t hal_led_init(void);

/**
 * @brief 直接设置 RGB 颜色并立即显示
 */
void hal_led_set_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief 设置 LED 模式（0=红, 1=绿, 2=蓝），自动打开 LED
 */
void hal_led_set_mode(uint8_t mode);

/**
 * @brief 设置亮度 0-255
 */
void hal_led_set_brightness(uint8_t brightness);

/**
 * @brief 相对调整亮度（+/-），自动处理边界
 */
void hal_led_adjust_brightness(int delta);

/** @brief 打开 LED（恢复上次模式） */
void hal_led_on(void);

/** @brief 关闭 LED */
void hal_led_off(void);

/** @brief 切换开关 */
void hal_led_toggle(void);

/** @brief 当前是否打开 */
bool hal_led_is_on(void);

/** @brief 当前模式 */
uint8_t hal_led_get_mode(void);

/** @brief 当前亮度 */
uint8_t hal_led_get_brightness(void);

#ifdef __cplusplus
}
#endif
