/*
 * ESP32-S31-Korvo: GIF动画 + RGB LED控制
 *
 * 功能概述:
 *   - 左半屏(0-399): 循环播放GIF动图
 *   - 右半屏(400-799): 三个圆形按钮控制板载RGB LED
 *
 * 硬件依赖:
 *   - ESP32-S31-Korvo开发板 (4.3寸 800x480 RGB屏 + GT911触摸)
 *   - 16MB Flash + 16MB PSRAM
 *
 * 使用组件:
 *   - ESP-BSP          (显示/触摸/LED 初始化)
 *   - esp_lvgl_port    v2.8.0 (LVGL任务与线程安全)
 *   - LVGL             v8.4.0 + lv_gif 组件 (动图解码)
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"             /* esp_lvgl_port v2.8.0 */
#include "bsp/esp-bsp.h"               /* 官方BSP: 显示/触摸/LED */
#include "gif_player.h"                /* GIF动图播放器 */
#include "led_control.h"               /* RGB LED控制面板 */

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESP32-S31-Korvo 动图+RGB灯控制 v2.0");
    ESP_LOGI(TAG, "========================================");

    /* -------------------------------------------------------
     * 1. 初始化板载RGB LED (PWM调光, 初始熄灭)
     * ------------------------------------------------------- */
    ESP_LOGI(TAG, "正在初始化RGB LED...");
    ESP_ERROR_CHECK(bsp_led_init());

    /* -------------------------------------------------------
     * 2. 初始化显示 + LVGL + 触摸
     *
     *    bsp_display_start_with_config() 内部会依次:
     *      a) 初始化 LCD 硬件 (RGB接口 + ST7262初始化序列)
     *      b) 调用 esp_lvgl_port_init 创建 LVGL 主任务
     *      c) 注册 LVGL 显示设备 (已配置双缓冲/DMA/PSRAM)
     *      d) 初始化 GT911 触摸并注册 LVGL 输入设备
     *
     *    TODO #1: 如果编译报错 "bsp_display_config_t" 未定义,
     *     请检查 BSP 中实际的结构体名称, 可能是:
     *       - bsp_display_cfg_t
     *       - bsp_display_config_t
     *     查看路径: managed_components/espressif__esp-bsp/boards/*/
     *
     *    TODO #2: 如果 bsp_display_config_t 不包含 lvgl_port_cfg 字段,
     *     请先单独调用 esp_lvgl_port_init() 再调用此函数:
     *
     *       esp_lvgl_port_handle_t port = esp_lvgl_port_init(&(lvgl_port_cfg_t){
     *           .task_priority   = 4,
     *           .task_stack      = 8192,
     *           .task_affinity   = 0,
     *           .task_max_sleep_ms = 500,
     *           .timer_period_ms = 5,
     *       });
     * ------------------------------------------------------- */
    ESP_LOGI(TAG, "正在初始化显示与LVGL...");

    /* LVGL 端口配置 (绑定到 CPU0, 优先级 4, 栈 8192) */
    const lvgl_port_cfg_t lvgl_port_cfg = {
        .task_priority    = 4,          /* LVGL 主任务优先级 */
        .task_stack       = 8192,       /* LVGL 主任务栈大小 */
        .task_affinity    = 0,          /* 0=CPU0 */
        .task_max_sleep_ms = 500,       /* 最大休眠时间(ms) */
        .timer_period_ms  = 5,          /* LVGL 定时器周期(ms) */
    };

    /* 显示配置: 双缓冲 + DMA + PSRAM */
    const bsp_display_config_t bsp_disp_cfg = {
        .lvgl_port_cfg  = lvgl_port_cfg,           /* LVGL 任务配置 */
        .max_transfer_sz = BSP_LCD_H_RES * 100
                           * sizeof(lv_color_t),    /* 单次传输最大字节 */
        .double_buffer  = true,                     /* 双缓冲消除撕裂 */
        .buff_dma       = true,                     /* 启用 DMA 传输 */
        .buff_spiram    = true,                     /* 缓冲区分配在 PSRAM */
    };

    /* 启动显示 (阻塞直到初始化完成) */
    ESP_ERROR_CHECK(bsp_display_start_with_config(&bsp_disp_cfg));
    ESP_LOGI(TAG, "显示就绪: %dx%d @RGB565", BSP_LCD_H_RES, BSP_LCD_V_RES);

    /* -------------------------------------------------------
     * 3. 创建 UI (通过 BSP 提供的锁确保线程安全)
     *
     *    bsp_display_lock/unlock 内部调用
     *    esp_lvgl_port_lock/unlock, 确保在 LVGL
     *    任务外的线程也能安全操作 LVGL 对象。
     * ------------------------------------------------------- */
    bsp_display_lock();

    /* 设置屏幕背景 */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x111111), 0);

    /* 左侧: GIF 动图播放器 (0,0)-(399,479) */
    ESP_LOGI(TAG, "正在创建GIF播放器...");
    lv_obj_t *gif = gif_player_create(scr);
    if (gif == NULL) {
        ESP_LOGW(TAG, "GIF播放器创建失败, 请检查GIF文件路径和menuconfig设置");
    }

    /* 右侧: RGB LED 控制面板 (400,0)-(799,479) */
    ESP_LOGI(TAG, "正在创建LED控制面板...");
    led_control_create(scr);

    bsp_display_unlock();

    ESP_LOGI(TAG, "系统启动完成! 动图自动循环播放, 触摸按钮控制RGB LED");

    /* -------------------------------------------------------
     * 4. 主循环
     *
     *    esp_lvgl_port 内部已创建 LVGL 定时器任务 (周期5ms),
     *    自动调用 lv_timer_handler(), 无需手动维护。
     *    主任务只需保持存活即可。
     * ------------------------------------------------------- */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));    /* 每秒醒一次, 保持存活 */
    }
}
