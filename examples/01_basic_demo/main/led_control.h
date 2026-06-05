/*
 * RGB LED控制面板
 *
 * 提供三个圆形彩色按钮(红/绿/蓝), 控制板载RGB LED。
 * 基于官方 BSP 的 bsp_led_init() / bsp_led_set() 接口。
 * 触摸事件使用 LVGL 回调方式处理, 无轮询。
 */
#pragma once
#include "lvgl.h"

/**
 * @brief  在右侧区域创建 RGB LED 控制面板
 *
 * @param  parent  父对象 (通常为 lv_scr_act())
 * @return lv_obj_t*  面板容器对象指针
 *
 * @note   布局: 右侧 400px 宽, 顶部"RGB LED"文字,
 *         下方三个圆形按钮(红/绿/蓝), 直径120px, 垂直等距排列
 */
lv_obj_t *led_control_create(lv_obj_t *parent);
