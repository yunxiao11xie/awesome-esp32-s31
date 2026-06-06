/**
 * @file hal_display.h
 * @brief 显示子系统：RGB LCD + GT1151 触摸
 *
 * LCD 和触摸共享同一条 I2C 总线（触摸挂在 LCD 接口上），
 * 因此合并为一个 HAL 模块。I2C 总线生命周期由模块内部管理。
 */
#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 RGB LCD 并行面板
 *
 * 内部创建 I2C 总线（被触摸复用），配置 RGB565 16 位并行接口。
 * 初始化后面板处于关闭状态（disp_on_off(false)），
 * 由 app_ui 在 LVGL 就绪后统一开启。
 *
 * @param[out] out_panel LCD 面板句柄
 * @return ESP_OK 成功
 */
esp_err_t hal_display_lcd_init(esp_lcd_panel_handle_t *out_panel);

/**
 * @brief 初始化 GT1151 触摸（I2C）
 *
 * 复用 hal_display_lcd_init() 创建的 I2C 总线，
 * 必须在 LCD 初始化之后调用。
 *
 * @param[out] out_touch 触摸句柄
 * @return ESP_OK 成功
 */
esp_err_t hal_display_touch_init(esp_lcd_touch_handle_t *out_touch);

#ifdef __cplusplus
}
#endif
