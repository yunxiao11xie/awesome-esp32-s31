/**
 * @file app_logic.c
 * @brief 业务逻辑调度层
 *
 * 从 hal_key 队列读取按键事件，调度 hal_led 和 app_ui。
 * 这是唯一包含"按了什么键该做什么事"这个知识的地方。
 */
#include "bsp_board.h"
#include "app_logic.h"
#include "app_ui.h"
#include "hal_led.h"
#include "hal_key.h"

#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "app_logic";

/* ================================================================
 *  按键名称映射（仅用于屏幕显示）
 * ================================================================ */

static const char *key_name(key_event_t key)
{
    switch (key) {
    case KEY_EVENT_SET:       return "SET";
    case KEY_EVENT_MODE:      return "MODE";
    case KEY_EVENT_VOL_MINUS: return "VOL-";
    case KEY_EVENT_VOL_PLUS:  return "VOL+";
    default:                  return "NONE";
    }
}


/* ================================================================
 *  按键业务处理
 * ================================================================ */

void app_logic_key_task(void *arg)
{
    QueueHandle_t q = (QueueHandle_t)arg;
    key_event_msg_t msg;
    char status_buf[32];

    ESP_LOGI(TAG, "按键业务任务已启动");

    while (1) {
        if (xQueueReceive(q, &msg, portMAX_DELAY)) {
            /* 更新屏幕状态文字 */
            snprintf(status_buf, sizeof(status_buf), "%s %s",
                     key_name(msg.key),
                     msg.long_press ? "LONG" : "SHORT");
            app_ui_set_key_status(status_buf);

            ESP_LOGI(TAG, "KEY: %s %s", key_name(msg.key),
                     msg.long_press ? "LONG" : "SHORT");

            switch (msg.key) {

            case KEY_EVENT_SET:
                if (msg.long_press) {
                    hal_led_toggle();
                } else {
                    app_ui_toggle_gif_playing();
                }
                break;

            case KEY_EVENT_MODE:
                app_ui_set_gif_led_sync(true);          /* 用户按 MODE → 重新开启颜色同步 */
                if (msg.long_press) {
                    hal_led_set_mode(0);
                    hal_led_set_brightness(LED_BRIGHTNESS);
                    app_ui_set_gif_speed(1.0f);         /* 重置 GIF 速度 */
                } else {
                    hal_led_set_mode((hal_led_get_mode() + 1) % 3);
                }
                hal_led_on();
                break;

            case KEY_EVENT_VOL_MINUS:
                if (msg.long_press) {
                    /* 长按：GIF 减速 */
                    float cur = app_ui_get_gif_speed();
                    if (cur > 1.5f)      cur = 1.5f;
                    else if (cur > 1.0f) cur = 1.0f;
                    else if (cur > 0.5f) cur = 0.5f;
                    else                 cur = 0.25f;
                    app_ui_set_gif_speed(cur);
                } else {
                    hal_led_adjust_brightness(-20);
                }
                break;

            case KEY_EVENT_VOL_PLUS:
                if (msg.long_press) {
                    /* 长按：GIF 加速 */
                    float cur = app_ui_get_gif_speed();
                    if (cur < 0.25f)     cur = 0.25f;
                    else if (cur < 0.5f) cur = 0.5f;
                    else if (cur < 1.0f) cur = 1.0f;
                    else if (cur < 1.5f) cur = 1.5f;
                    else                 cur = 2.0f;
                    app_ui_set_gif_speed(cur);
                } else {
                    hal_led_adjust_brightness(20);
                }
                break;

            default:
                break;
            }
        }
    }
}
