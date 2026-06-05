/*
 * 跳舞小人动画: 左上角 80×80 区域
 */
#pragma once
#include "lvgl.h"

/* 创建动画, 放在屏幕左上角. 需要在 lvgl_port_init() 之后调用. */
lv_obj_t* dancing_man_create(lv_obj_t *parent);
