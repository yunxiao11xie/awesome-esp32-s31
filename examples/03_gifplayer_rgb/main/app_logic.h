/**
 * @file app_logic.h
 * @brief 业务逻辑调度层：从 hal_key 队列读取事件，调度 hal_led / app_ui
 *
 * 这是唯一"知道所有模块"的胶水层。hal_key / hal_led / app_ui 彼此不感知。
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 按键业务处理任务
 *
 * 从队列阻塞读取按键事件，解析后调用对应的 hal_led 或 app_ui 接口。
 * 可直接传给 xTaskCreate。
 *
 * @param arg QueueHandle_t 类型的按键事件队列
 */
void app_logic_key_task(void *arg);

#ifdef __cplusplus
}
#endif
