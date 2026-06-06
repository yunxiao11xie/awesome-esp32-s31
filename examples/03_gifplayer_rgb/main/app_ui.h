/**
 * @file app_ui.h
 * @brief 用户界面模块：LVGL 初始化 + UI 创建 + 对外控制接口
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 LVGL 并创建所有 UI 元素
 *
 * 内部流程：LVGL 端口 → 注册显示设备 → 注册触摸 → 渐变背景 → GIF → 面板
 *
 * @param lcd   LCD 面板句柄（来自 hal_display）
 * @param touch 触摸句柄（来自 hal_display）
 * @return ESP_OK 成功
 */
esp_err_t app_ui_init(esp_lcd_panel_handle_t lcd, esp_lcd_touch_handle_t touch);

/**
 * @brief 控制 GIF 播放/暂停
 *
 * 播放时 LED 指示灯绿色呼吸；暂停时暗红常亮。
 */
void app_ui_set_gif_playing(bool playing);

/**
 * @brief 切换 GIF 播放/暂停（不传递状态，由模块内部管理）
 */
void app_ui_toggle_gif_playing(void);

/**
 * @brief 设置 GIF 播放速度倍率
 *
 * @param ratio 速度倍率，范围 0.25 ~ 2.0，1.0 = 原始速度
 */
void app_ui_set_gif_speed(float ratio);

/**
 * @brief 获取当前 GIF 播放速度倍率
 * @return 当前倍率 (0.25 ~ 2.0)
 */
float app_ui_get_gif_speed(void);

/**
 * @brief 切换 GIF 动图
 *
 * @param index 动图索引 (0 ~ count-1)，超出范围会回绕
 */
void app_ui_switch_gif(int index);

/**
 * @brief 获取动图总数
 * @return 动图数量
 */
int app_ui_get_gif_count(void);

/**
 * @brief 更新按键状态显示文字
 */
void app_ui_set_key_status(const char *text);

/**
 * @brief 开关 GIF→LED 颜色同步
 *
 * 默认开启。用户点击 RGB 按钮后会关闭同步；按 MODE 键重新开启。
 *
 * @param enabled true=LED 跟随 GIF 主色调呼吸, false=LED 由 RGB 按钮控制
 */
void app_ui_set_gif_led_sync(bool enabled);

#ifdef __cplusplus
}
#endif
