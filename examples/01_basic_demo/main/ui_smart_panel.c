/*
 * Smart Panel UI: RGB LED control with 3 color buttons
 * Uses montserrat_14 (only font compiled by default)
 */
#include "ui_smart_panel.h"
#include "ws2812.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "UI_PANEL";
static led_state_t current_led = LED_STATE_OFF;
static lv_obj_t *status_label = NULL;

#define BTN_W          160
#define BTN_H          56
#define BTN_RADIUS     28

/* ===================== Set LED via WS2812 ===================== */
static void set_led_color(led_state_t state)
{
    switch (state) {
        case LED_STATE_RED:   ws2812_set_color_rgb(255, 0, 0); break;
        case LED_STATE_GREEN: ws2812_set_color_rgb(0, 255, 0); break;
        case LED_STATE_BLUE:  ws2812_set_color_rgb(0, 0, 255); break;
        default:              ws2812_set_color_rgb(0, 0, 0);   break;
    }
    current_led = state;
    if (status_label) {
        const char *name = (state == LED_STATE_RED)   ? "On: RED"  :
                           (state == LED_STATE_GREEN) ? "On: GREEN":
                           (state == LED_STATE_BLUE)  ? "On: BLUE" : "Off";
        lv_label_set_text(status_label, name);
    }
}

/* ===================== Button event handler ===================== */
static void btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);
    led_state_t led = (led_state_t)(intptr_t)lv_obj_get_user_data(obj);

    if (code == LV_EVENT_PRESSED) {
        lv_obj_set_size(obj, BTN_W - 8, BTN_H - 4);
        lv_obj_set_style_bg_opa(obj, LV_OPA_90, 0);
    }
    if (code == LV_EVENT_RELEASED) {
        lv_obj_set_size(obj, BTN_W, BTN_H);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    }
    if (code == LV_EVENT_SHORT_CLICKED) {
        set_led_color(led);
    }
}

/* ===================== Create a styled button ===================== */
static lv_obj_t *create_color_btn(lv_obj_t *parent, const char *text,
                                   lv_color_t bg, lv_color_t glow,
                                   led_state_t target)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(btn, BTN_W, BTN_H);
    lv_obj_set_style_radius(btn, BTN_RADIUS, 0);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, glow, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_40, 0);
    lv_obj_set_style_shadow_color(btn, glow, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_50, 0);
    lv_obj_set_style_shadow_width(btn, 20, 0);
    lv_obj_set_style_shadow_ofs_y(btn, 5, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, glow, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_70, LV_STATE_PRESSED);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);

    lv_obj_set_user_data(btn, (void *)(intptr_t)target);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_ALL, NULL);
    return btn;
}

/* ===================== Public API ===================== */
lv_obj_t *ui_smart_panel_create(lv_obj_t *parent)
{
    ESP_LOGI(TAG, "Creating smart panel UI");

    /* Background - 覆盖全屏但不可点击, 避免拦截触摸事件 */
    lv_obj_t *bg = lv_obj_create(parent);
    lv_obj_remove_style_all(bg);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bg, 800, 480);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x0F0F1A), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_move_background(bg);

    /* Right-side card panel - 不可点击/滚动, 避免拦截子控件的触摸事件 */
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(panel, 300, 440);
    lv_obj_align(panel, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 20, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x3A3A5C), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_pad_all(panel, 20, 0);
    lv_obj_set_style_pad_top(panel, 25, 0);
    /* clip_corner 在 LVGL 8 中不可用, 移除 */

    /* Title */
    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "Smart LED");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    /* Separator */
    lv_obj_t *sep = lv_obj_create(panel);
    lv_obj_remove_style_all(sep);
    lv_obj_set_size(sep, 220, 1);
    lv_obj_align_to(sep, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x3A3A5C), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);

    /* Color buttons */
    lv_obj_t *b1 = create_color_btn(panel, "RED",
        lv_color_hex(0xD32F2F), lv_color_hex(0xFF5555), LED_STATE_RED);
    lv_obj_align_to(b1, sep, LV_ALIGN_OUT_BOTTOM_MID, 0, 30);

    lv_obj_t *b2 = create_color_btn(panel, "GREEN",
        lv_color_hex(0x2E7D32), lv_color_hex(0x55FF55), LED_STATE_GREEN);
    lv_obj_align_to(b2, b1, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

    lv_obj_t *b3 = create_color_btn(panel, "BLUE",
        lv_color_hex(0x1565C0), lv_color_hex(0x5555FF), LED_STATE_BLUE);
    lv_obj_align_to(b3, b2, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

    /* Status */
    status_label = lv_label_create(panel);
    set_led_color(LED_STATE_OFF);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xA0A0C0), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_align_to(status_label, b3, LV_ALIGN_OUT_BOTTOM_MID, 0, 25);

    /* Off button */
    lv_obj_t *btn_off = lv_obj_create(panel);
    lv_obj_remove_style_all(btn_off);
    lv_obj_add_flag(btn_off, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn_off, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(btn_off, 100, 36);
    lv_obj_align_to(btn_off, status_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 15);
    lv_obj_set_style_radius(btn_off, 18, 0);
    lv_obj_set_style_bg_color(btn_off, lv_color_hex(0x333344), 0);
    lv_obj_set_style_bg_opa(btn_off, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn_off, lv_color_hex(0x555577), 0);
    lv_obj_set_style_border_width(btn_off, 1, 0);
    lv_obj_set_style_bg_color(btn_off, lv_color_hex(0x555577), LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(btn_off, 0, 0);

    lv_obj_t *off_lbl = lv_label_create(btn_off);
    lv_label_set_text(off_lbl, "OFF");
    lv_obj_center(off_lbl);
    lv_obj_set_style_text_color(off_lbl, lv_color_hex(0x8888AA), 0);
    lv_obj_set_style_text_font(off_lbl, &lv_font_montserrat_14, 0);

    lv_obj_add_event_cb(btn_off, btn_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_user_data(btn_off, (void *)(intptr_t)LED_STATE_OFF);

    ESP_LOGI(TAG, "Smart panel UI created");
    return panel;
}

led_state_t ui_smart_panel_get_led_state(void) { return current_led; }
