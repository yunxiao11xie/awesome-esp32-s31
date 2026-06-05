/*
 * LVGL 移植: v8.3 最小化 - 无 mutex, 单任务直接驱动
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_cache.h"
#include "lvgl.h"
#include "lvgl_port.h"

static const char *TAG = "LVGL";
static uint16_t *fb = NULL;

static lv_disp_drv_t disp_drv;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[800 * 32], buf2[800 * 32];

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    int w = area->x2 - area->x1 + 1, h = area->y2 - area->y1 + 1;
    for (int y = 0; y < h; y++)
        memcpy(&fb[(area->y1 + y) * 800 + area->x1], &color_p[y * w], w * sizeof(lv_color_t));
    esp_cache_msync(&fb[area->y1 * 800], 800 * h * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    lv_disp_flush_ready(drv);
}

esp_err_t lvgl_port_init(esp_lcd_panel_handle_t panel, uint16_t *frame_buf)
{
    fb = frame_buf;
    lv_init();

    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, 800 * 32);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 800;
    disp_drv.ver_res = 480;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    /* 设默认黑底 */
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);

    ESP_LOGI(TAG, "LVGL port ready");
    return ESP_OK;
}

/* 直接在 app_main 调用, 不复用单独任务 */
void lvgl_port_handler(void)
{
    lv_tick_inc(5);
    lv_timer_handler();
}

void lvgl_port_lock(void)   {}
void lvgl_port_unlock(void) {}
