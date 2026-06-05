/*
 * GIF动画播放器
 *
 * 基于 LVGL v8.4.0 官方 lv_gif 组件
 * - GIF 数据存储在 Flash 中 (通过 CMake EMBED_FILES 嵌入)
 * - 运行时加载到 PSRAM, 不占用内部 SRAM
 * - 自动无限循环播放
 *
 * 需在 menuconfig 中启用: CONFIG_LV_USE_GIF=y
 */
#pragma once
#include "lvgl.h"

/**
 * @brief  在左侧区域创建 GIF 动图播放器
 *
 * @param  parent  父对象 (通常为 lv_scr_act())
 * @return lv_obj_t*  GIF 对象指针, 失败返回 NULL
 *
 * @note   动图文件通过 CMakeLists.txt 的 EMBED_FILES 嵌入固件
 *         TODO: 将项目根目录下的 my_animation.gif 替换为实际的 GIF 文件
 */
lv_obj_t *gif_player_create(lv_obj_t *parent);
