/**
 * @file hal_key.h
 * @brief ADC 按键硬件抽象层
 *
 * 纯硬件读取：ADC 扫描 → 消抖 → 分类 → 队列发送。
 * 不含任何业务逻辑（业务调度在 app_logic 中完成）。
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 按键事件枚举（与内部 ADC 分类值对齐） */
typedef enum {
    KEY_EVENT_NONE      = 0,
    KEY_EVENT_SET,
    KEY_EVENT_MODE,
    KEY_EVENT_VOL_MINUS,
    KEY_EVENT_VOL_PLUS,
} key_event_t;

/** @brief 按键消息结构 */
typedef struct {
    key_event_t key;
    bool        long_press;
} key_event_msg_t;

/**
 * @brief 初始化 ADC 按键阵列并启动扫描任务
 *
 * 内部创建 FreeRTOS 任务，稳定检测到按键后通过队列发送事件。
 * 队列满时丢弃（不阻塞硬件扫描）。
 *
 * @param notify_queue 接收按键事件的队列句柄（由调用方创建）
 * @return ESP_OK 成功
 */
esp_err_t hal_key_init(QueueHandle_t notify_queue);

#ifdef __cplusplus
}
#endif
