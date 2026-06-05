/*
 * LVGL 移植: v8.3 PARTIAL 渲染模式
 *
 * RGB LCD 的 DMA 持续从 PSRAM 帧缓冲读取数据输出到屏幕
 * LVGL 渲染到独立的 PSRAM 绘制缓冲 (PARTIAL 模式)
 * flush 回调将绘制数据 memcpy 到帧缓冲, 然后 esp_cache_msync
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_cache.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "lcd_st7262.h"

static const char *TAG = "LVGL";
static uint16_t *fb = NULL;

/* 绘制缓冲大小: 每次渲染 50 行 (800*50*2 = 80KB) */
#define DRAW_BUF_LINES  50

static lv_disp_drv_t disp_drv;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *draw_buf1 = NULL;
static lv_color_t *draw_buf2 = NULL;

/* ==================== LVGL flush 回调 ==================== */
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                           lv_color_t *color_p)
{
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;

    /* 将渲染数据从绘制缓冲复制到帧缓冲 */
    if (w == LCD_H_RES) {
        /* 全宽: 直接复制整块 (最常见) */
        memcpy(&fb[area->y1 * LCD_H_RES], color_p, w * h * sizeof(lv_color_t));
    } else {
        /* 部分宽度: 逐行复制 */
        for (int y = 0; y < h; y++) {
            memcpy(&fb[(area->y1 + y) * LCD_H_RES + area->x1],
                   &color_p[y * w], w * sizeof(lv_color_t));
        }
    }

    /* 同步 CPU 缓存到 PSRAM, 确保 DMA 可见 */
    size_t sync_size = h * LCD_H_RES * 2;
    esp_cache_msync(&fb[area->y1 * LCD_H_RES], sync_size,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M);

    lv_disp_flush_ready(drv);
}

/* ==================== 初始化 ==================== */
esp_err_t lvgl_port_init(esp_lcd_panel_handle_t panel, uint16_t *frame_buf)
{
    fb = frame_buf;
    ESP_LOGI(TAG, "Frame buffer at %p", (void *)fb);

    lv_init();

    /* 分配 PSRAM 绘制缓冲 (64字节对齐以兼容缓存操作) */
    size_t buf_size = LCD_H_RES * DRAW_BUF_LINES * sizeof(lv_color_t);
    draw_buf1 = heap_caps_aligned_alloc(64, buf_size, MALLOC_CAP_SPIRAM);
    draw_buf2 = heap_caps_aligned_alloc(64, buf_size, MALLOC_CAP_SPIRAM);
    if (!draw_buf1 || !draw_buf2) {
        ESP_LOGE(TAG, "Failed to allocate draw buffers from PSRAM");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Draw buffers: buf1=%p buf2=%p (%d lines each)",
             (void *)draw_buf1, (void *)draw_buf2, DRAW_BUF_LINES);

    lv_disp_draw_buf_init(&draw_buf, draw_buf1, draw_buf2, LCD_H_RES * DRAW_BUF_LINES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    ESP_LOGI(TAG, "LVGL port ready (v8 PARTIAL mode, %d lines/buf)", DRAW_BUF_LINES);
    return ESP_OK;
}

/* ==================== 主循环驱动 ==================== */
void lvgl_port_handler(void)
{
    /* 用真实经过时间驱动 LVGL 时钟 */
    static int64_t last_tick_us = 0;
    int64_t now_us = esp_timer_get_time();
    if (last_tick_us == 0) last_tick_us = now_us;
    int32_t elapsed_ms = (int32_t)((now_us - last_tick_us) / 1000);
    if (elapsed_ms > 0) {
        lv_tick_inc(elapsed_ms);
        last_tick_us = now_us;
    }

    lv_timer_handler();
}

void lvgl_port_lock(void)   {}
void lvgl_port_unlock(void) {}
