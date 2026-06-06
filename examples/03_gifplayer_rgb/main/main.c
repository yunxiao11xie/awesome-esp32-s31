/**
 * @file main.c
 * @brief ESP32-S31-Korvo 动图播放器 + RGB LED — 主程序入口
 *
 * 仅负责模块初始化编排，不含业务逻辑。
 *
 * 初始化顺序：
 *   1. LCD 面板（RGB 并行接口）
 *   2. GT1151 触摸（I2C）
 *   3. LVGL + UI（渐变背景、GIF、控制面板）
 *   4. WS2812 LED
 *   5. ADC 按键（启动扫描任务）
 *   6. 按键业务处理（启动调度任务）
 */
#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "bsp_board.h"
#include "hal_display.h"
#include "hal_led.h"
#include "hal_key.h"
#include "app_ui.h"
#include "app_logic.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  ESP32-S31-Korvo 动图播放器 + RGB LED");
    ESP_LOGI(TAG, "  屏幕: %dx%d RGB565", LCD_H_RES, LCD_V_RES);
    ESP_LOGI(TAG, "============================================");

    /* ---- 1. LCD ---- */
    esp_lcd_panel_handle_t lcd = NULL;
    ESP_ERROR_CHECK(hal_display_lcd_init(&lcd));

    /* ---- 2. 触摸 ---- */
    esp_lcd_touch_handle_t touch = NULL;
    esp_err_t touch_err = hal_display_touch_init(&touch);
    if (touch_err != ESP_OK) {
        ESP_LOGW(TAG, "触摸初始化失败（无触摸交互）");
    }

    /* ---- 3. LVGL + UI ---- */
    ESP_ERROR_CHECK(app_ui_init(lcd, touch));

    /* ---- 4. RGB LED ---- */
    esp_err_t led_err = hal_led_init();
    if (led_err != ESP_OK) {
        ESP_LOGW(TAG, "LED 初始化跳过: %s", esp_err_to_name(led_err));
    }

    /* ---- 5. ADC 按键 ---- */
    QueueHandle_t key_queue = xQueueCreate(8, sizeof(key_event_msg_t));
    if (key_queue == NULL) {
        ESP_LOGE(TAG, "按键事件队列创建失败");
        return;
    }
    esp_err_t key_err = hal_key_init(key_queue);
    if (key_err != ESP_OK) {
        ESP_LOGW(TAG, "ADC 按键未启动: %s", esp_err_to_name(key_err));
    }

    /* ---- 6. 按键业务调度 ---- */
    xTaskCreate(app_logic_key_task, "app_logic", 4096, key_queue, 5, NULL);

    ESP_LOGI(TAG, "全部初始化完成！LVGL 任务已在 CPU0 运行");
}
