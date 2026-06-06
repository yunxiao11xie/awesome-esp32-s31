/**
 * @file app_ui.c
 * @brief LVGL 图形界面：背景、GIF 播放器、控制面板、按钮交互
 *
 * 所有 UI 对象指针（lv_obj_t *）封存在本模块内部，
 * 外部通过 app_ui_xxx() 接口间接控制。
 */
#include "bsp_board.h"
#include "app_ui.h"
#include "hal_led.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "extra/libs/gif/lv_gif.h"
#include <string.h>
#include <stdlib.h>
#include "driver/temperature_sensor.h"

static const char *TAG = "app_ui";

/* ---- LVGL 显示设备 ---- */
static lv_disp_t *lvgl_disp = NULL;

/* ---- GIF 播放器 ---- */
static lv_obj_t *gif_obj            = NULL;
static lv_obj_t *gif_status_label   = NULL;
static lv_obj_t *led_dot            = NULL;
static bool      gif_playing        = true;

/* ---- 控制按钮 ---- */
static lv_obj_t *btn_red   = NULL;
static lv_obj_t *btn_green = NULL;
static lv_obj_t *btn_blue  = NULL;

/* ---- 按键状态文字 ---- */
static lv_obj_t *key_status_label = NULL;

/* ---- HUD 系统仪表盘 ---- */
static lv_obj_t *hud_panel    = NULL;
static lv_obj_t *hud_fps_lbl  = NULL;
static lv_obj_t *hud_mem_lbl  = NULL;
static lv_obj_t *hud_cpu_lbl  = NULL;
static lv_obj_t *hud_fps_dot  = NULL;
static lv_obj_t *hud_mem_dot  = NULL;
static lv_obj_t *hud_cpu_dot  = NULL;

#define HUD_UPDATE_MS  500

/* ---- 温度传感器 ---- */
static temperature_sensor_handle_t temp_sensor = NULL;

/* ---- GIF 速度控制 ---- */
static float     gif_speed_ratio  = 1.0f;
static uint32_t  gif_base_period  = 100;
static lv_obj_t *speed_badge       = NULL;
static lv_obj_t *speed_badge_lbl   = NULL;
static lv_timer_t *speed_hide_tmr  = NULL;

/* ---- GIF 动图列表 ---- */
typedef struct {
    const uint8_t *start;
    const uint8_t *end;
    const char    *name;
} gif_entry_t;

static const gif_entry_t gif_list[] = {
    { _binary_dance_1_gif_start, _binary_dance_1_gif_end, "dance 1" },
    { _binary_dance_2_gif_start, _binary_dance_2_gif_end, "dance 2" },
    { _binary_dance_3_gif_start, _binary_dance_3_gif_end, "dance 3" },
};
static const int gif_count = 3;
static int       gif_current = 0;

/* ---- GIF 切换 UI 控件 ---- */
static lv_obj_t *gif_sel_name  = NULL;
static lv_obj_t *gif_sel_idx   = NULL;

/* ---- PSRAM GIF 缓冲 + 图像描述符 ---- */
static uint8_t     *gif_buf      = NULL;
static lv_img_dsc_t gif_img_dsc;

/* ---- HUD 更新回调前向声明 ---- */
static void hud_update_cb(lv_timer_t *tmr);
static void create_color_ring(lv_obj_t *parent);


/* ---- GIF 颜色追踪 + 色环指示器 ---- */
#define GIF_TRACK_PERIOD_MS   33       /* ~30 FPS 追踪频率 */
#define GIF_SAMPLE_INTERVAL   10       /* 每 10 tick 采样一次 (~330ms) */
#define GIF_BREATHE_PERIOD_MS 2500     /* 呼吸周期 2.5s */
#define COLOR_LERP_STEP       12       /* 颜色插值步进 (~600ms 满量程) */

static bool     gif_led_sync    = true;  /* GIF→LED 同步开关 */
static lv_obj_t *color_ring_arc = NULL;
static lv_obj_t *color_ring_dot = NULL;
static uint8_t   gif_track_r      = 0;   /* GIF 主色调 (目标色) */
static uint8_t   gif_track_g      = 0;
static uint8_t   gif_track_b      = 0;
static uint8_t   gif_cur_r        = 0;   /* LED 当前色 (插值缓冲) */
static uint8_t   gif_cur_g        = 0;
static uint8_t   gif_cur_b        = 0;
static uint32_t  breathe_tick     = 0;   /* 呼吸计时器 */


/* ================================================================
 *  LED 呼吸动画回调
 * ================================================================ */

static void led_dot_anim_cb(void *var, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}


/* ================================================================
 *  渐变暗角背景
 * ================================================================ */

static void create_vignette_background(void)
{
    const int w = LCD_H_RES;
    const int h = LCD_V_RES;
    const int cx = w / 2;
    const int cy = h / 2;
    const int inner_r = 250;
    const int outer_r = 500;

    /* ---- 分配 PSRAM 缓冲区 ---- */
    lv_color_t *buf = (lv_color_t *)heap_caps_malloc(
        w * h * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        ESP_LOGE(TAG, "背景: PSRAM 分配失败 (%zu)",
                 w * h * sizeof(lv_color_t));
        return;
    }

    const lv_color_t base = lv_color_make(26, 29, 38);   /* #1A1D26 */
    const lv_color_t dark = lv_color_make(5, 8, 12);     /* #05080C */

    /* ---- 逐像素径向暗角 ---- */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int dx = (x > cx) ? (x - cx) : (cx - x);
            int dy = (y > cy) ? (y - cy) : (cy - y);
            int dist = (dx > dy) ? (dx + dy / 2) : (dy + dx / 2);

            if (dist <= inner_r) {
                buf[y * w + x] = base;
            } else if (dist >= outer_r) {
                buf[y * w + x] = dark;
            } else {
                int t = (dist - inner_r) * 255 / (outer_r - inner_r);
                buf[y * w + x] = lv_color_mix(base, dark, (uint8_t)t);
            }
        }
    }

    /* ---- 胶片噪点 ---- */
    uint32_t rng = 0xDEADBEEF;
    const int total = w * h;
    for (int i = 0; i < total; i += 35) {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;

        uint16_t *p = &((uint16_t *)buf)[i];
        uint8_t r = (*p >> 11) & 0x1F;
        uint8_t g = (*p >> 5) & 0x3F;
        uint8_t b = *p & 0x1F;

        int dr = (int)(rng % 3) - 1;
        int dg = (int)((rng >> 2) % 3) - 1;
        int db = (int)((rng >> 4) % 3) - 1;

        if (dr > 0 && r < 31) r++; else if (dr < 0 && r > 0) r--;
        if (dg > 0 && g < 63) g++; else if (dg < 0 && g > 0) g--;
        if (db > 0 && b < 31) b++; else if (db < 0 && b > 0) b--;

        *p = (r << 11) | (g << 5) | b;
    }

    /* ---- 注册为 LVGL 图像 ---- */
    static lv_img_dsc_t img_dsc;
    img_dsc.header.cf       = LV_IMG_CF_TRUE_COLOR;
    img_dsc.header.w        = w;
    img_dsc.header.h        = h;
    img_dsc.data            = (const uint8_t *)buf;
    img_dsc.data_size       = w * h * sizeof(lv_color_t);

    lv_obj_t *bg = lv_img_create(lv_scr_act());
    if (bg == NULL) {
        ESP_LOGE(TAG, "背景图创建失败");
        heap_caps_free(buf);
        return;
    }
    lv_img_set_src(bg, &img_dsc);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_move_background(bg);

    ESP_LOGI(TAG, "渐变暗角背景已创建 (%dx%d + 噪点)", w, h);
}


/* ================================================================
 *  涟漪动效（按钮按下时水波纹扩散）
 * ================================================================ */

static void ripple_zoom_cb(void *var, int32_t v)
{
    lv_obj_set_style_transform_zoom((lv_obj_t *)var, v, 0);
}

static void ripple_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void ripple_del_cb(lv_anim_t *a)
{
    lv_obj_t *ripple = (lv_obj_t *)a->var;
    lv_obj_del(ripple);
}

static void btn_ripple_start(lv_obj_t *btn)
{
    /* 创建涟漪子对象（白色圆形） */
    lv_obj_t *ripple = lv_obj_create(btn);
    if (ripple == NULL) return;

    lv_obj_set_size(ripple, BTN_DIAMETER, BTN_DIAMETER);
    lv_obj_set_pos(ripple, 0, 0);
    lv_obj_set_style_radius(ripple, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ripple, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(ripple, LV_OPA_40, 0);
    lv_obj_set_style_border_width(ripple, 0, 0);
    lv_obj_set_style_pad_all(ripple, 0, 0);
    lv_obj_clear_flag(ripple, LV_OBJ_FLAG_SCROLLABLE);

    /* 缩放动画：0 → 256 (100%)，350ms，缓出 */
    lv_anim_t a_zoom;
    lv_anim_init(&a_zoom);
    lv_anim_set_var(&a_zoom, ripple);
    lv_anim_set_exec_cb(&a_zoom, ripple_zoom_cb);
    lv_anim_set_values(&a_zoom, 0, 256);
    lv_anim_set_time(&a_zoom, 350);
    lv_anim_set_path_cb(&a_zoom, lv_anim_path_ease_out);
    lv_anim_start(&a_zoom);

    /* 透明度动画：40% → 0%，500ms，缓入，结束后删除 */
    lv_anim_t a_opa;
    lv_anim_init(&a_opa);
    lv_anim_set_var(&a_opa, ripple);
    lv_anim_set_exec_cb(&a_opa, ripple_opa_cb);
    lv_anim_set_values(&a_opa, LV_OPA_40, LV_OPA_TRANSP);
    lv_anim_set_time(&a_opa, 500);
    lv_anim_set_path_cb(&a_opa, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&a_opa, ripple_del_cb);
    lv_anim_start(&a_opa);
}


/* ================================================================
 *  圆形按钮事件回调
 * ================================================================ */

static void btn_event_cb(lv_event_t *evt)
{
    lv_event_code_t code = lv_event_get_code(evt);
    lv_obj_t *btn = lv_event_get_target(evt);
    uint32_t color_idx = (uint32_t)(uintptr_t)lv_obj_get_user_data(btn);

    if (code == LV_EVENT_PRESSED) {
        lv_obj_set_style_transform_zoom(btn, ZOOM_PRESSED, 0);
        btn_ripple_start(btn);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_obj_set_style_transform_zoom(btn, ZOOM_NORMAL, 0);
    } else if (code == LV_EVENT_CLICKED) {
        gif_led_sync = false;       /* 用户手动调色 → 关闭 GIF→LED 同步 */
        switch (color_idx) {
        case 0: hal_led_set_rgb(LED_BRIGHTNESS, 0, 0);          break;
        case 1: hal_led_set_rgb(0, LED_BRIGHTNESS, 0);          break;
        case 2: hal_led_set_rgb(0, 0, LED_BRIGHTNESS);          break;
        default:                                                 break;
        }
    }
}


/* ================================================================
 *  圆形按钮创建
 * ================================================================ */

static lv_obj_t *create_round_button(lv_color_t color, lv_color_t color_dim,
                                     lv_coord_t cx, lv_coord_t cy,
                                     uint32_t idx, lv_obj_t *parent)
{
    lv_obj_t *btn = lv_btn_create(parent ? parent : lv_scr_act());
    if (btn == NULL) return NULL;

    lv_obj_set_size(btn, BTN_DIAMETER, BTN_DIAMETER);
    lv_obj_set_pos(btn, cx - BTN_DIAMETER / 2, cy - BTN_DIAMETER / 2);

    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, lv_color_white(), 0);
    lv_obj_set_style_border_width(btn, 3, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, color_dim, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn, 8, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_white(), 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_30, 0);
    lv_obj_set_style_shadow_ofs_x(btn, 0, 0);
    lv_obj_set_style_shadow_ofs_y(btn, 0, 0);
    lv_obj_set_style_clip_corner(btn, true, 0);

    lv_obj_set_user_data(btn, (void *)(uintptr_t)idx);

    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_PRESSED,   NULL);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_RELEASED,  NULL);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED,   NULL);

    return btn;
}


/* ================================================================
 *  GIF 动图切换
 * ================================================================ */

static void switch_gif(int index)
{
    if (gif_obj == NULL) return;

    /* 回绕 */
    if (index < 0) index = gif_count - 1;
    if (index >= gif_count) index = 0;

    /* ---- 释放旧 PSRAM 缓冲 ---- */
    if (gif_buf != NULL) {
        heap_caps_free(gif_buf);
        gif_buf = NULL;
    }

    /* ---- 分配并复制新 GIF 数据 ---- */
    size_t size = (size_t)(gif_list[index].end - gif_list[index].start);
    if (size == 0) {
        ESP_LOGE(TAG, "GIF[%d] 数据为空！", index);
        return;
    }
    gif_buf = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (gif_buf == NULL) {
        ESP_LOGE(TAG, "switch_gif[%d]: PSRAM 分配失败 (%zu)", index, size);
        return;
    }
    memcpy(gif_buf, gif_list[index].start, size);

    /* ---- 更新图像描述符 ---- */
    gif_img_dsc.header.cf  = LV_IMG_CF_RAW;
    gif_img_dsc.data       = gif_buf;
    gif_img_dsc.data_size  = size;

    /* ---- 设置新源 ---- */
    lv_gif_set_src(gif_obj, &gif_img_dsc);
    lv_obj_center(gif_obj);
    lv_img_set_zoom(gif_obj, ZOOM_NORMAL);
    gif_current = index;

    /* ---- 记录基准帧间隔 ---- */
    {
        lv_gif_t *data = (lv_gif_t *)gif_obj;
        if (data->gif != NULL) {
            uint32_t delay_cs = data->gif->gce.delay;
            gif_base_period = (delay_cs > 0) ? (delay_cs * 10) : 100;
            if (gif_base_period < 20) gif_base_period = 100;
            ESP_LOGI(TAG, "GIF[%d] 基准帧间隔: %lu ms", index, (unsigned long)gif_base_period);
        }
    }

    /* ---- 恢复速度倍率 ---- */
    if (gif_speed_ratio != 1.0f) {
        lv_gif_t *data = (lv_gif_t *)gif_obj;
        if (data->timer != NULL) {
            uint32_t new_period = (uint32_t)(gif_base_period / gif_speed_ratio);
            lv_timer_set_period(data->timer, new_period < 10 ? 10 : new_period);
        }
    }

    /* ---- 恢复播放/暂停状态 ---- */
    lv_gif_t *data = (lv_gif_t *)gif_obj;
    if (!gif_playing && data->timer != NULL) {
        lv_timer_pause(data->timer);
    }

    /* ---- 更新选择器文字 ---- */
    if (gif_sel_name != NULL) {
        lv_label_set_text(gif_sel_name, gif_list[index].name);
    }
    if (gif_sel_idx != NULL) {
        lv_label_set_text_fmt(gif_sel_idx, "%d / %d", index + 1, gif_count);
    }

    ESP_LOGI(TAG, "GIF 已切换到 [%s] (#%d)", gif_list[index].name, index);
}


/* ================================================================
 *  GIF 容器 + 电影遮幅 + 呼吸 LED
 * ================================================================ */

static esp_err_t create_gif_container(void)
{
    /* ---- 裁剪容器（玻璃卡片） ---- */
    lv_obj_t *cont = lv_obj_create(lv_scr_act());
    if (cont == NULL) return ESP_FAIL;

    lv_obj_set_pos(cont, GIF_AREA_X, GIF_AREA_Y);
    lv_obj_set_size(cont, GIF_AREA_W, GIF_AREA_H);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_radius(cont, 16, 0);
    lv_obj_set_style_clip_corner(cont, true, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* 玻璃卡片 */
    lv_obj_set_style_bg_color(cont, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_30, 0);
    lv_obj_set_style_border_color(cont, lv_color_white(), 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_border_opa(cont, 64, 0);

    /* 悬浮阴影 */
    lv_obj_set_style_shadow_width(cont, 16, 0);
    lv_obj_set_style_shadow_ofs_y(cont, 6, 0);
    lv_obj_set_style_shadow_ofs_x(cont, 0, 0);
    lv_obj_set_style_shadow_color(cont, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(cont, 89, 0);

    /* ---- GIF 动图 ---- */
    gif_obj = lv_gif_create(cont);
    if (gif_obj == NULL) {
        ESP_LOGE(TAG, "lv_gif_create 失败");
        lv_obj_del(cont);
        return ESP_FAIL;
    }
    gif_playing = true;

    /* ---- 电影遮幅：顶部黑条 ---- */
    lv_obj_t *top_bar = lv_obj_create(cont);
    lv_obj_set_size(top_bar, GIF_AREA_W, LETTERBOX_BAR_H);
    lv_obj_set_style_bg_color(top_bar, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_radius(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 0, 0);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 8; i++) {
        lv_obj_t *h = lv_obj_create(top_bar);
        lv_obj_set_size(h, 8, 8);
        lv_obj_set_style_bg_color(h, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(h, 38, 0);
        lv_obj_set_style_border_width(h, 0, 0);
        lv_obj_set_style_radius(h, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_all(h, 0, 0);
        lv_obj_set_pos(h, 20 + i * 50, (LETTERBOX_BAR_H - 8) / 2);
        lv_obj_clear_flag(h, LV_OBJ_FLAG_SCROLLABLE);
    }

    /* ---- 电影遮幅：底部黑条 ---- */
    lv_obj_t *bot_bar = lv_obj_create(cont);
    lv_obj_set_size(bot_bar, GIF_AREA_W, LETTERBOX_BAR_H);
    lv_obj_set_style_bg_color(bot_bar, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bot_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bot_bar, 0, 0);
    lv_obj_set_style_radius(bot_bar, 0, 0);
    lv_obj_set_style_pad_all(bot_bar, 0, 0);
    lv_obj_align(bot_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(bot_bar, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 8; i++) {
        lv_obj_t *h = lv_obj_create(bot_bar);
        lv_obj_set_size(h, 8, 8);
        lv_obj_set_style_bg_color(h, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(h, 38, 0);
        lv_obj_set_style_border_width(h, 0, 0);
        lv_obj_set_style_radius(h, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_all(h, 0, 0);
        lv_obj_set_pos(h, 20 + i * 50, (LETTERBOX_BAR_H - 8) / 2);
        lv_obj_clear_flag(h, LV_OBJ_FLAG_SCROLLABLE);
    }

    /* ---- 状态胶囊 ---- */
    gif_status_label = lv_label_create(cont);
    if (gif_status_label != NULL) {
        lv_label_set_text(gif_status_label, "GIF PLAYING");
        lv_obj_set_style_text_color(gif_status_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(gif_status_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_color(gif_status_label, lv_color_make(0, 0, 0), 0);
        lv_obj_set_style_bg_opa(gif_status_label, LV_OPA_70, 0);
        lv_obj_set_style_pad_hor(gif_status_label, 14, 0);
        lv_obj_set_style_pad_ver(gif_status_label, 6, 0);
        lv_obj_set_style_radius(gif_status_label, LV_RADIUS_CIRCLE, 0);
        lv_obj_align(gif_status_label, LV_ALIGN_BOTTOM_MID, 0, -4);
    }

    /* ---- 呼吸 LED ---- */
    led_dot = lv_obj_create(cont);
    if (led_dot != NULL) {
        lv_obj_set_size(led_dot, 12, 12);
        lv_obj_set_style_radius(led_dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(led_dot, 0, 0);
        lv_obj_set_style_pad_all(led_dot, 0, 0);
        lv_obj_set_style_bg_color(led_dot, lv_color_make(0, 220, 0), 0);
        lv_obj_set_style_bg_opa(led_dot, LV_OPA_90, 0);
        lv_obj_align_to(led_dot, gif_status_label, LV_ALIGN_OUT_LEFT_MID, -8, 0);
        lv_obj_clear_flag(led_dot, LV_OBJ_FLAG_SCROLLABLE);

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, led_dot);
        lv_anim_set_exec_cb(&a, led_dot_anim_cb);
        lv_anim_set_values(&a, LV_OPA_30, 242);
        lv_anim_set_time(&a, 1800);
        lv_anim_set_playback_time(&a, 1800);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    }

    /* ---- 加载第一个 GIF ---- */
    switch_gif(0);

    ESP_LOGI(TAG, "GIF 动图容器已创建 (%d 个动图)", gif_count);
    return ESP_OK;
}


/* ================================================================
 *  右侧控制面板（横排 RGB 按钮 + GIF 选择器）
 * ================================================================ */

static void gif_prev_cb(lv_event_t *evt)
{
    (void)evt;
    if (!lvgl_port_lock(0)) return;
    switch_gif(gif_current - 1);
    lvgl_port_unlock();
}
static void gif_next_cb(lv_event_t *evt)
{
    (void)evt;
    if (!lvgl_port_lock(0)) return;
    switch_gif(gif_current + 1);
    lvgl_port_unlock();
}

static esp_err_t create_control_panel(void)
{
    /* ---- 面板容器（玻璃卡片） ---- */
    lv_obj_t *panel = lv_obj_create(lv_scr_act());
    if (panel == NULL) return ESP_FAIL;

    lv_obj_set_pos(panel, RIGHT_PANEL_X, 0);
    lv_obj_set_size(panel, RIGHT_PANEL_W, LCD_V_RES);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_color(panel, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_50, 0);
    lv_obj_set_style_border_color(panel, lv_color_white(), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_opa(panel, 64, 0);
    lv_obj_set_style_radius(panel, 16, 0);

    lv_obj_set_style_shadow_width(panel, 12, 0);
    lv_obj_set_style_shadow_ofs_y(panel, 4, 0);
    lv_obj_set_style_shadow_ofs_x(panel, 0, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(panel, 115, 0);

    /* ---- 标题（加大 font 28） ---- */
    lv_obj_t *title = lv_label_create(panel);
    if (title == NULL) return ESP_FAIL;
    lv_label_set_text(title, "◉ GIF + RGB");
    lv_obj_set_style_text_color(title, lv_color_make(51, 51, 51), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, RP_TITLE_Y);

    /* ---- 按键状态 ---- */
    key_status_label = lv_label_create(panel);
    if (key_status_label == NULL) return ESP_FAIL;
    lv_label_set_text(key_status_label, "KEY READY");
    lv_obj_set_style_text_color(key_status_label, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_text_font(key_status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(key_status_label, LV_ALIGN_TOP_MID, 0, RP_STATUS_Y);

    /* ---- 装饰分隔线 ---- */
    lv_obj_t *sep1 = lv_obj_create(panel);
    lv_obj_set_size(sep1, 160, 1);
    lv_obj_set_style_bg_color(sep1, lv_color_make(180, 180, 180), 0);
    lv_obj_set_style_bg_opa(sep1, LV_OPA_40, 0);
    lv_obj_set_style_border_width(sep1, 0, 0);
    lv_obj_set_style_radius(sep1, 0, 0);
    lv_obj_align(sep1, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_clear_flag(sep1, LV_OBJ_FLAG_SCROLLABLE);

    /* ==============================================================
     *  GIF 选择器
     * ============================================================== */

    lv_obj_t *sel_title = lv_label_create(panel);
    lv_label_set_text(sel_title, "SELECT GIF");
    lv_obj_set_style_text_color(sel_title, lv_color_make(80, 80, 80), 0);
    lv_obj_set_style_text_font(sel_title, &lv_font_montserrat_14, 0);
    lv_obj_align(sel_title, LV_ALIGN_TOP_MID, 0, 116);

    /* ◀ 左箭头 */
    lv_obj_t *arr_left = lv_btn_create(panel);
    lv_obj_set_size(arr_left, RP_ARROW_DIAM, RP_ARROW_DIAM);
    lv_obj_set_style_radius(arr_left, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(arr_left, lv_color_make(60, 60, 60), 0);
    lv_obj_set_style_bg_opa(arr_left, LV_OPA_60, 0);
    lv_obj_set_style_border_width(arr_left, 0, 0);
    lv_obj_set_style_pad_all(arr_left, 0, 0);
    lv_obj_align(arr_left, LV_ALIGN_TOP_MID, -74, 141);
    lv_obj_clear_flag(arr_left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *arr_l_lbl = lv_label_create(arr_left);
    lv_label_set_text(arr_l_lbl, "<");
    lv_obj_center(arr_l_lbl);
    lv_obj_set_style_text_color(arr_l_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(arr_l_lbl, &lv_font_montserrat_20, 0);
    lv_obj_add_event_cb(arr_left, gif_prev_cb, LV_EVENT_CLICKED, NULL);

    /* ▶ 右箭头 */
    lv_obj_t *arr_right = lv_btn_create(panel);
    lv_obj_set_size(arr_right, RP_ARROW_DIAM, RP_ARROW_DIAM);
    lv_obj_set_style_radius(arr_right, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(arr_right, lv_color_make(60, 60, 60), 0);
    lv_obj_set_style_bg_opa(arr_right, LV_OPA_60, 0);
    lv_obj_set_style_border_width(arr_right, 0, 0);
    lv_obj_set_style_pad_all(arr_right, 0, 0);
    lv_obj_align(arr_right, LV_ALIGN_TOP_MID, 74, 141);
    lv_obj_clear_flag(arr_right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *arr_r_lbl = lv_label_create(arr_right);
    lv_label_set_text(arr_r_lbl, ">");
    lv_obj_center(arr_r_lbl);
    lv_obj_set_style_text_color(arr_r_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(arr_r_lbl, &lv_font_montserrat_20, 0);
    lv_obj_add_event_cb(arr_right, gif_next_cb, LV_EVENT_CLICKED, NULL);

    /* GIF 名称标签 */
    gif_sel_name = lv_label_create(panel);
    lv_label_set_text(gif_sel_name, "--");
    lv_obj_set_style_text_color(gif_sel_name, lv_color_make(220, 220, 220), 0);
    lv_obj_set_style_text_font(gif_sel_name, &lv_font_montserrat_20, 0);
    lv_obj_align(gif_sel_name, LV_ALIGN_TOP_MID, 0, RP_GIF_NAME_Y);

    /* 页码 */
    gif_sel_idx = lv_label_create(panel);
    lv_label_set_text(gif_sel_idx, "-- / --");
    lv_obj_set_style_text_color(gif_sel_idx, lv_color_make(130, 130, 130), 0);
    lv_obj_set_style_text_font(gif_sel_idx, &lv_font_montserrat_14, 0);
    lv_obj_align(gif_sel_idx, LV_ALIGN_TOP_MID, 0, RP_GIF_IDX_Y);

    /* ---- 装饰分隔线 2 ---- */
    lv_obj_t *sep2 = lv_obj_create(panel);
    lv_obj_set_size(sep2, 160, 1);
    lv_obj_set_style_bg_color(sep2, lv_color_make(180, 180, 180), 0);
    lv_obj_set_style_bg_opa(sep2, LV_OPA_40, 0);
    lv_obj_set_style_border_width(sep2, 0, 0);
    lv_obj_set_style_radius(sep2, 0, 0);
    lv_obj_align(sep2, LV_ALIGN_TOP_MID, 0, 216);
    lv_obj_clear_flag(sep2, LV_OBJ_FLAG_SCROLLABLE);

    /* ==============================================================
     *  RGB 灯光按钮（横排）
     * ============================================================== */

    /* 颜色标签 */
    lv_obj_t *lbl_hint = lv_label_create(panel);
    lv_label_set_text(lbl_hint, "LIGHTING");
    lv_obj_set_style_text_color(lbl_hint, lv_color_make(80, 80, 80), 0);
    lv_obj_set_style_text_font(lbl_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_hint, LV_ALIGN_TOP_MID, 0, 236);

    /* 三个圆按钮的横向中心位置 (右面板 400px 宽) */
    const lv_coord_t btn_cx[] = { 78, 200, 322 };
    const lv_color_t btn_clr[] = {
        lv_color_make(255, 0, 0),   /* 红 */
        lv_color_make(0, 255, 0),   /* 绿 */
        lv_color_make(0, 0, 255),   /* 蓝 */
    };
    const lv_color_t btn_dim[] = {
        lv_color_make(102, 0, 0),
        lv_color_make(0, 102, 0),
        lv_color_make(0, 0, 102),
    };
    const char *btn_names[] = { "RED", "GREEN", "BLUE" };

    lv_obj_t *btns[3];
    for (int i = 0; i < 3; i++) {
        btns[i] = create_round_button(btn_clr[i], btn_dim[i],
                                       btn_cx[i], RP_RGB_Y, i, panel);
        if (btns[i] == NULL) return ESP_FAIL;

        /* 按钮下方颜色文字标签 */
        lv_obj_t *lbl = lv_label_create(panel);
        lv_label_set_text(lbl, btn_names[i]);
        lv_obj_set_style_text_color(lbl, lv_color_make(200, 200, 200), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_align_to(lbl, btns[i], LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    }

    btn_red   = btns[0];
    btn_green = btns[1];
    btn_blue  = btns[2];

    /* ---- 色环指示器（面板底部） ---- */
    create_color_ring(panel);

    ESP_LOGI(TAG, "控制面板已重建（横排布局）");
    return ESP_OK;
}


/* ================================================================
 *  LVGL 端口初始化
 * ================================================================ */

static esp_err_t lvgl_port_setup(esp_lcd_panel_handle_t lcd,
                                  esp_lcd_touch_handle_t touch)
{
    lvgl_port_cfg_t lvgl_cfg = {
        .task_priority   = 4,
        .task_stack      = 8192,
        .task_affinity   = 0,
        .task_max_sleep_ms = 10,
        .timer_period_ms = 5,
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL 端口初始化失败");

    /* ---- 注册显示设备 ---- */
    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = NULL,
        .panel_handle  = lcd,
        .buffer_size   = LCD_H_RES * 100,
        .double_buffer = true,
        .hres          = LCD_H_RES,
        .vres          = LCD_V_RES,
        .flags = {
            .buff_dma   = true,
            .buff_spiram = true,
        },
    };
    lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode       = 0,
            .avoid_tearing = 0,
        },
    };
    lvgl_disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (lvgl_disp == NULL) {
        ESP_LOGE(TAG, "显示设备注册失败");
        return ESP_FAIL;
    }

    /* ---- 注册触摸 ---- */
    if (touch != NULL) {
        lvgl_port_touch_cfg_t touch_cfg = {
            .disp   = lvgl_disp,
            .handle = touch,
        };
        if (lvgl_port_add_touch(&touch_cfg) == NULL) {
            ESP_LOGW(TAG, "触摸注册失败（继续）");
        }
    }

    /* ---- 开启显示 ---- */
    esp_lcd_panel_disp_on_off(lcd, true);

    ESP_LOGI(TAG, "LVGL 端口初始化完成");
    return ESP_OK;
}


/* ================================================================
 *  HUD 系统仪表盘（FPS / 内存 / 温度）
 * ================================================================ */

static void hud_update_cb(lv_timer_t *tmr)
{
    (void)tmr;

    /* ---- FPS：从 LVGL 显示空闲时间推算 ---- */
    uint32_t inactive_ms = lv_disp_get_inactive_time(lvgl_disp);
    float instant_fps = inactive_ms > 0 ? 1000.0f / inactive_ms : 0;
    static float smooth_fps = 0;
    smooth_fps = smooth_fps * 0.85f + instant_fps * 0.15f;

    uint32_t fps_int = (uint32_t)(smooth_fps + 0.5f);
    lv_label_set_text_fmt(hud_fps_lbl, "FPS %u", (unsigned)fps_int);
    if (smooth_fps >= 28) {
        lv_obj_set_style_bg_color(hud_fps_dot, lv_color_make(0, 220, 0), 0);
    } else if (smooth_fps >= 15) {
        lv_obj_set_style_bg_color(hud_fps_dot, lv_color_make(220, 180, 0), 0);
    } else {
        lv_obj_set_style_bg_color(hud_fps_dot, lv_color_make(220, 0, 0), 0);
    }

    /* ---- 内存：PSRAM 使用量 (MB) ---- */
    uint32_t free_ps  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t total_ps = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    uint32_t used_ps  = total_ps - free_ps;
    uint32_t used_mb  = used_ps / (1024 * 1024);
    uint32_t used_mb_dec = (used_ps % (1024 * 1024)) / ((uint32_t)(1024 * 1024) / 10);
    uint32_t total_mb = total_ps / (1024 * 1024);

    lv_label_set_text_fmt(hud_mem_lbl, "MEM %u.%u/%uM",
                          (unsigned)used_mb, (unsigned)used_mb_dec, (unsigned)total_mb);
    uint8_t used_pct = (uint8_t)((used_ps * 100) / total_ps);
    if (used_pct < 40) {
        lv_obj_set_style_bg_color(hud_mem_dot, lv_color_make(0, 220, 0), 0);
    } else if (used_pct < 70) {
        lv_obj_set_style_bg_color(hud_mem_dot, lv_color_make(220, 180, 0), 0);
    } else {
        lv_obj_set_style_bg_color(hud_mem_dot, lv_color_make(220, 0, 0), 0);
    }

    /* ---- 温度：CPU 内部温度 ---- */
    float tsens_val;
    esp_err_t tsens_err = temperature_sensor_get_celsius(temp_sensor, &tsens_val);
    if (tsens_err == ESP_OK) {
        int temp_int = (int)(tsens_val + 0.5f);
        lv_label_set_text_fmt(hud_cpu_lbl, "TEMP %d°C", temp_int);
        if (temp_int < 50) {
            lv_obj_set_style_bg_color(hud_cpu_dot, lv_color_make(0, 220, 0), 0);
        } else if (temp_int < 65) {
            lv_obj_set_style_bg_color(hud_cpu_dot, lv_color_make(220, 180, 0), 0);
        } else {
            lv_obj_set_style_bg_color(hud_cpu_dot, lv_color_make(220, 0, 0), 0);
        }
    } else {
        lv_label_set_text_fmt(hud_cpu_lbl, "TEMP --");
        lv_obj_set_style_bg_color(hud_cpu_dot, lv_color_make(100, 100, 100), 0);
    }
}

static void create_hud_dashboard(void)
{
    /* ---- 面板：右上角半透明胶囊 ---- */
    hud_panel = lv_obj_create(lv_scr_act());
    lv_obj_set_size(hud_panel, 124, 74);
    lv_obj_set_pos(hud_panel, LCD_H_RES - 130, LCD_V_RES - 79);
    lv_obj_set_style_radius(hud_panel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hud_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(hud_panel, LV_OPA_50, 0);
    lv_obj_set_style_border_width(hud_panel, 1, 0);
    lv_obj_set_style_border_color(hud_panel, lv_color_white(), 0);
    lv_obj_set_style_border_opa(hud_panel, 30, 0);
    lv_obj_set_style_pad_all(hud_panel, 0, 0);
    lv_obj_clear_flag(hud_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(hud_panel);

    /* ---- FPS 行 ---- */
    hud_fps_dot = lv_obj_create(hud_panel);
    lv_obj_set_size(hud_fps_dot, 6, 6);
    lv_obj_set_style_radius(hud_fps_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(hud_fps_dot, 0, 0);
    lv_obj_set_style_pad_all(hud_fps_dot, 0, 0);
    lv_obj_set_pos(hud_fps_dot, 12, 10);
    lv_obj_set_style_bg_color(hud_fps_dot, lv_color_make(0, 220, 0), 0);
    lv_obj_clear_flag(hud_fps_dot, LV_OBJ_FLAG_SCROLLABLE);

    hud_fps_lbl = lv_label_create(hud_panel);
    lv_obj_set_pos(hud_fps_lbl, 24, 7);
    lv_label_set_text(hud_fps_lbl, "FPS --");
    lv_obj_set_style_text_color(hud_fps_lbl, lv_color_make(200, 200, 200), 0);
    lv_obj_set_style_text_font(hud_fps_lbl, &lv_font_montserrat_14, 0);

    /* ---- MEM 行 ---- */
    hud_mem_dot = lv_obj_create(hud_panel);
    lv_obj_set_size(hud_mem_dot, 6, 6);
    lv_obj_set_style_radius(hud_mem_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(hud_mem_dot, 0, 0);
    lv_obj_set_style_pad_all(hud_mem_dot, 0, 0);
    lv_obj_set_pos(hud_mem_dot, 12, 30);
    lv_obj_set_style_bg_color(hud_mem_dot, lv_color_make(100, 150, 255), 0);
    lv_obj_clear_flag(hud_mem_dot, LV_OBJ_FLAG_SCROLLABLE);

    hud_mem_lbl = lv_label_create(hud_panel);
    lv_obj_set_pos(hud_mem_lbl, 24, 27);
    lv_label_set_text(hud_mem_lbl, "MEM --");
    lv_obj_set_style_text_color(hud_mem_lbl, lv_color_make(200, 200, 200), 0);
    lv_obj_set_style_text_font(hud_mem_lbl, &lv_font_montserrat_14, 0);

    /* ---- TEMP 行 ---- */
    hud_cpu_dot = lv_obj_create(hud_panel);
    lv_obj_set_size(hud_cpu_dot, 6, 6);
    lv_obj_set_style_radius(hud_cpu_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(hud_cpu_dot, 0, 0);
    lv_obj_set_style_pad_all(hud_cpu_dot, 0, 0);
    lv_obj_set_pos(hud_cpu_dot, 12, 50);
    lv_obj_set_style_bg_color(hud_cpu_dot, lv_color_make(200, 200, 50), 0);
    lv_obj_clear_flag(hud_cpu_dot, LV_OBJ_FLAG_SCROLLABLE);

    hud_cpu_lbl = lv_label_create(hud_panel);
    lv_obj_set_pos(hud_cpu_lbl, 24, 47);
    lv_label_set_text(hud_cpu_lbl, "TEMP --");
    lv_obj_set_style_text_color(hud_cpu_lbl, lv_color_make(200, 200, 200), 0);
    lv_obj_set_style_text_font(hud_cpu_lbl, &lv_font_montserrat_14, 0);

    /* ---- 定时更新 ---- */
    lv_timer_create(hud_update_cb, HUD_UPDATE_MS, NULL);

    /* ---- 温度传感器初始化 ---- */
    temp_sensor = NULL;
    temperature_sensor_config_t tsens_cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&tsens_cfg, &temp_sensor) == ESP_OK) {
        temperature_sensor_enable(temp_sensor);
        ESP_LOGI(TAG, "温度传感器已启用");
    } else {
        ESP_LOGW(TAG, "温度传感器不可用，TEMP 将显示 --");
    }
}


/* ================================================================
 *  GIF 播放速度控制 + 速度徽章
 * ================================================================ */

/* 动画包装器：lv_anim_exec_xcb_t 签名是 void(*)(void*, int32_t) */
static void badge_opa_anim_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}
static void badge_zoom_anim_cb(void *var, int32_t v)
{
    lv_obj_set_style_transform_zoom((lv_obj_t *)var, v, 0);
}

static void speed_badge_fade_cb(lv_anim_t *a)
{
    lv_obj_t *obj = (lv_obj_t *)a->var;
    if (a->current_value <= LV_OPA_TRANSP) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void speed_hide_timer_cb(lv_timer_t *tmr)
{
    (void)tmr;
    /* 1.5 秒无操作 → 淡出徽章 */
    if (speed_badge == NULL) return;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, speed_badge);
    lv_anim_set_exec_cb(&a, badge_opa_anim_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&a, 500);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&a, speed_badge_fade_cb);
    lv_anim_start(&a);
}

static void show_speed_badge(void)
{
    if (speed_badge == NULL || speed_badge_lbl == NULL) return;

    /* 清除之前的隐藏/淡出 */
    lv_anim_del(speed_badge, NULL);
    lv_obj_clear_flag(speed_badge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(speed_badge, LV_OPA_COVER, 0);

    /* 更新文字 */
    lv_label_set_text_fmt(speed_badge_lbl, "%.1fx", (double)gif_speed_ratio);

    /* 颜色编码 */
    lv_color_t bg;
    if (gif_speed_ratio < 0.8f) {
        bg = lv_color_make(30, 60, 180);   /* 慢速 → 蓝色 */
    } else if (gif_speed_ratio > 1.3f) {
        bg = lv_color_make(180, 100, 20);   /* 快速 → 橙色 */
    } else {
        bg = lv_color_make(60, 60, 60);     /* 正常 → 灰色 */
    }
    lv_obj_set_style_bg_color(speed_badge, bg, 0);

    /* 弹出动画（缩放 0.8 → 1.0） */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, speed_badge);
    lv_anim_set_exec_cb(&a, badge_zoom_anim_cb);
    lv_anim_set_values(&a, 204, 256);   /* 0.8x → 1.0x */
    lv_anim_set_time(&a, 300);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    /* 每次显示都重建一次性隐藏定时器（1.5 秒后淡出） */
    if (speed_hide_tmr != NULL) {
        lv_timer_del(speed_hide_tmr);
    }
    speed_hide_tmr = lv_timer_create(speed_hide_timer_cb, 1500, NULL);
    lv_timer_set_repeat_count(speed_hide_tmr, 1);
}

static void create_speed_badge(void)
{
    speed_badge = lv_obj_create(lv_scr_act());
    lv_obj_set_size(speed_badge, 50, 28);
    lv_obj_set_style_radius(speed_badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(speed_badge, lv_color_make(60, 60, 60), 0);
    lv_obj_set_style_bg_opa(speed_badge, LV_OPA_80, 0);
    lv_obj_set_style_border_width(speed_badge, 0, 0);
    lv_obj_set_style_pad_all(speed_badge, 0, 0);
    lv_obj_set_style_opa(speed_badge, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(speed_badge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(speed_badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(speed_badge);
    /* 定位在 GIF 区域底部中央（与状态胶囊同一对齐基准） */
    lv_obj_set_pos(speed_badge, GIF_AREA_X + GIF_AREA_W/2 - 25,
                   LCD_V_RES - LETTERBOX_BAR_H - 40);

    speed_badge_lbl = lv_label_create(speed_badge);
    lv_label_set_text(speed_badge_lbl, "1.0x");
    lv_obj_center(speed_badge_lbl);
    lv_obj_set_style_text_color(speed_badge_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(speed_badge_lbl, &lv_font_montserrat_14, 0);

    /* 自动隐藏定时器（延迟到首次速度变化时创建） */
    speed_hide_tmr = NULL;
}

void app_ui_set_gif_speed(float ratio)
{
    /* 钳位到 [0.25, 2.0] */
    if (ratio < 0.25f) ratio = 0.25f;
    if (ratio > 2.0f)  ratio = 2.0f;

    if (!lvgl_port_lock(pdMS_TO_TICKS(100))) return;

    gif_speed_ratio = ratio;

    if (gif_obj != NULL && gif_base_period > 0) {
        lv_gif_t *data = (lv_gif_t *)gif_obj;
        if (data->timer != NULL) {
            uint32_t new_period = (uint32_t)(gif_base_period / ratio);
            if (new_period < 10) new_period = 10;
            lv_timer_set_period(data->timer, new_period);
        }
    }

    show_speed_badge();

    lvgl_port_unlock();
}

float app_ui_get_gif_speed(void)
{
    return gif_speed_ratio;
}

void app_ui_set_gif_led_sync(bool enabled)
{
    gif_led_sync = enabled;
}

void app_ui_switch_gif(int index)
{
    if (!lvgl_port_lock(0)) return;

    /* 确保索引在有效范围 */
    if (index < 0)           index = gif_count - 1;
    else if (index >= gif_count) index = 0;

    if (index != gif_current) {
        switch_gif(index);
    }

    lvgl_port_unlock();
}

int app_ui_get_gif_count(void)
{
    return gif_count;
}


/* ================================================================
 *  GIF 颜色追踪 + 色环指示器
 * ================================================================ */

static void create_color_ring(lv_obj_t *parent)
{
    /* ---- 色环容器 ---- */
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 110, 100);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, -36);

    /* ---- 外圈：lv_arc 色环 ---- */
    color_ring_arc = lv_arc_create(cont);
    lv_obj_set_size(color_ring_arc, 72, 72);
    lv_arc_set_range(color_ring_arc, 0, 100);
    lv_arc_set_value(color_ring_arc, 100);
    lv_arc_set_bg_angles(color_ring_arc, 0, 360);
    lv_arc_set_rotation(color_ring_arc, 90);
    lv_obj_remove_style(color_ring_arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(color_ring_arc, 8, 0);
    lv_obj_set_style_arc_width(color_ring_arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(color_ring_arc, lv_color_make(80, 80, 80), 0);
    lv_obj_set_style_arc_color(color_ring_arc, lv_color_make(100, 100, 100), LV_PART_INDICATOR);
    lv_obj_center(color_ring_arc);

    /* ---- 内圈：亮度指示点 ---- */
    color_ring_dot = lv_obj_create(cont);
    lv_obj_set_size(color_ring_dot, 20, 20);
    lv_obj_set_style_radius(color_ring_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(color_ring_dot, 0, 0);
    lv_obj_set_style_bg_color(color_ring_dot, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(color_ring_dot, LV_OPA_60, 0);
    lv_obj_center(color_ring_dot);

    /* ---- 文字标签 ---- */
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "  LED SYNC");
    lv_obj_set_style_text_color(lbl, lv_color_make(130, 130, 130), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align_to(lbl, cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
}


static void gif_sample_color(gd_GIF *gif)
{
    /* 降采样统计：将 GIF 当前帧缩放到 ~20×24 网格，取频率最高色 */
    const int sample_w = 20;
    const int sample_h = 24;

    uint8_t *canvas = gif->canvas;
    int w = gif->width;
    int h = gif->height;

    int step_x = (w > sample_w) ? (w / sample_w) : 1;
    int step_y = (h > sample_h) ? (h / sample_h) : 1;

    /* 颜色直方桶 */
    #define MAX_BUCKETS 64
    typedef struct { uint32_t rgb; uint32_t cnt; } bucket_t;
    bucket_t buckets[MAX_BUCKETS];
    int nb = 0;

    for (int y = 0; y < h; y += step_y) {
        for (int x = 0; x < w; x += step_x) {
            int off = (y * w + x) * 4;
            uint8_t a = canvas[off + 3];
            if (a < 128) continue;          /* 跳过透明像素 */

            uint8_t r = canvas[off + 0];
            uint8_t g = canvas[off + 1];
            uint8_t b = canvas[off + 2];

            /* 量化至 5-bit/通道 → 32K 桶空间，实际通常 < 64 */
            uint32_t qrgb = ((uint32_t)(r >> 3) << 10)
                          | ((uint32_t)(g >> 3) << 5)
                          | (uint32_t)(b >> 3);

            int found = -1;
            for (int i = 0; i < nb; i++) {
                if (buckets[i].rgb == qrgb) { found = i; break; }
            }
            if (found >= 0) {
                buckets[found].cnt++;
            } else if (nb < MAX_BUCKETS) {
                buckets[nb].rgb = qrgb;
                buckets[nb].cnt = 1;
                nb++;
            }
        }
    }

    /* 找频率最高的桶 */
    if (nb == 0) return;                    /* 全透明帧，保留上次颜色 */

    int best = 0;
    for (int i = 1; i < nb; i++) {
        if (buckets[i].cnt > buckets[best].cnt) best = i;
    }

    uint32_t qrgb = buckets[best].rgb;
    /* 5-bit → 8-bit 反量化 */
    gif_track_r = (uint8_t)(((qrgb >> 10) & 0x1F) << 3);
    gif_track_g = (uint8_t)(((qrgb >> 5)  & 0x1F) << 3);
    gif_track_b = (uint8_t)((qrgb         & 0x1F) << 3);
}


static void gif_color_track_cb(lv_timer_t *tmr)
{
    (void)tmr;

    breathe_tick += GIF_TRACK_PERIOD_MS;

    /* ---- 1. 采样主色调（每 10 tick 一次） ---- */
    static uint32_t sample_div = 0;
    sample_div++;
    if (sample_div >= GIF_SAMPLE_INTERVAL) {
        sample_div = 0;
        if (gif_obj != NULL && gif_playing) {
            lv_gif_t *data = (lv_gif_t *)gif_obj;
            if (data->gif != NULL && data->gif->canvas != NULL) {
                gif_sample_color(data->gif);
            }
        }
    }

    /* ---- 2. 当前色缓动 → 目标色 ---- */
    if (gif_playing) {
        #define LERP(c, t) do { \
            int32_t d = (int32_t)(t) - (int32_t)(c); \
            if (d > COLOR_LERP_STEP)      (c) += COLOR_LERP_STEP; \
            else if (d < -COLOR_LERP_STEP) (c) -= COLOR_LERP_STEP; \
            else                          (c) = (t); \
        } while(0)
        LERP(gif_cur_r, gif_track_r);
        LERP(gif_cur_g, gif_track_g);
        LERP(gif_cur_b, gif_track_b);
        #undef LERP
    }

    /* ---- 3. 呼吸正弦波（2.5s 周期） ---- */
    uint32_t angle_deg = (breathe_tick % GIF_BREATHE_PERIOD_MS) * 360 / GIF_BREATHE_PERIOD_MS;
    int32_t sin_val = lv_trigo_sin((int16_t)angle_deg);          /* -32767 .. 32767 */
    uint32_t sin_norm = (uint32_t)(sin_val + LV_TRIGO_SIN_MAX); /* 0 .. 65534  */
    uint8_t breathe = (uint8_t)(102 + (uint32_t)153 * sin_norm / 65534); /* 102..255 */

    /* ---- 4. 设置 LED（GIF 播放 + 同步开启时生效） ---- */
    if (gif_playing && gif_led_sync) {
        uint8_t led_r = (uint16_t)gif_cur_r * breathe / 255;
        uint8_t led_g = (uint16_t)gif_cur_g * breathe / 255;
        uint8_t led_b = (uint16_t)gif_cur_b * breathe / 255;
        hal_led_set_rgb(led_r, led_g, led_b);
    }

    /* ---- 5. 更新屏幕色环 ---- */
    if (color_ring_arc != NULL) {
        lv_color_t c = lv_color_make(gif_cur_r, gif_cur_g, gif_cur_b);
        lv_obj_set_style_arc_color(color_ring_arc, c, LV_PART_INDICATOR);
    }
    if (color_ring_dot != NULL) {
        lv_obj_set_style_bg_opa(color_ring_dot, breathe, 0);
    }
}


/* ================================================================
 *  对外接口
 * ================================================================ */

esp_err_t app_ui_init(esp_lcd_panel_handle_t lcd, esp_lcd_touch_handle_t touch)
{
    /* 1. LVGL 基础初始化 */
    ESP_ERROR_CHECK(lvgl_port_setup(lcd, touch));

    /* 2. 渐变暗角背景（最底层） */
    create_vignette_background();

    /* 3. GIF 动图 + 遮幅（左侧） */
    esp_err_t gif_err = create_gif_container();
    if (gif_err != ESP_OK) {
        ESP_LOGW(TAG, "GIF 容器创建失败");
        lv_obj_t *err = lv_label_create(lv_scr_act());
        lv_label_set_text(err, "GIF Load Error\nCheck file path");
        lv_obj_set_pos(err, 50, 200);
        lv_obj_set_style_text_color(err, lv_color_make(255, 100, 100), 0);
    }

    /* 4. 控制面板（右侧） */
    ESP_ERROR_CHECK(create_control_panel());

    /* 5. HUD 系统仪表盘（右上角） */
    create_hud_dashboard();

    /* 6. GIF 速度徽章（叠加层） */
    create_speed_badge();

    /* 7. GIF 颜色追踪定时器（~30 FPS） */
    lv_timer_create(gif_color_track_cb, GIF_TRACK_PERIOD_MS, NULL);

    ESP_LOGI(TAG, "UI 初始化全部完成");
    return ESP_OK;
}

void app_ui_set_gif_playing(bool playing)
{
    gif_playing = playing;

    if (!lvgl_port_lock(0)) {
        ESP_LOGW(TAG, "LVGL lock 失败");
        return;
    }

    if (gif_obj != NULL) {
        lv_gif_t *data = (lv_gif_t *)gif_obj;
        if (data->timer != NULL) {
            if (playing) {
                lv_timer_resume(data->timer);
                lv_timer_reset(data->timer);
            } else {
                lv_timer_pause(data->timer);
            }
        }
    }

    if (gif_status_label != NULL) {
        lv_label_set_text(gif_status_label,
                          playing ? "GIF PLAYING" : "GIF PAUSED");
    }

    if (led_dot != NULL) {
        if (playing) {
            lv_obj_set_style_bg_color(led_dot, lv_color_make(0, 220, 0), 0);
            lv_anim_del(led_dot, NULL);
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, led_dot);
            lv_anim_set_exec_cb(&a, led_dot_anim_cb);
            lv_anim_set_values(&a, LV_OPA_30, 242);
            lv_anim_set_time(&a, 1800);
            lv_anim_set_playback_time(&a, 1800);
            lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
            lv_anim_start(&a);
        } else {
            lv_anim_del(led_dot, NULL);
            lv_obj_set_style_bg_color(led_dot, lv_color_make(160, 20, 20), 0);
            lv_obj_set_style_bg_opa(led_dot, LV_OPA_70, 0);
        }
    }

    lvgl_port_unlock();
}

void app_ui_toggle_gif_playing(void)
{
    app_ui_set_gif_playing(!gif_playing);
}

void app_ui_set_key_status(const char *text)
{
    if (!lvgl_port_lock(0)) return;
    if (key_status_label != NULL) {
        lv_label_set_text(key_status_label, text);
    }
    lvgl_port_unlock();
}
