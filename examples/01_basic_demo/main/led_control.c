/*
 * RGB LED控制面板实现
 *
 * 布局 (右侧 400-799):
 *   ┌──────────────────────┐
 *   │      RGB LED          │  <- 标题文字, 水平居中
 *   │──────────────────────│  <- 分隔线
 *   │                        │
 *   │     ┌────────┐         │  <- 红色圆形按钮 (直径120px, y=100)
 *   │     │   RED   │         │
 *   │     └────────┘         │
 *   │         │              │  <- 间距 40px
 *   │     ┌────────┐         │  <- 绿色圆形按钮 (y=260)
 *   │     │  GREEN  │         │
 *   │     └────────┘         │
 *   │         │              │  <- 间距 40px
 *   │     ┌────────┐         │  <- 蓝色圆形按钮 (y=420)
 *   │     │  BLUE   │         │
 *   │     └────────┘         │
 *   └──────────────────────┘
 *
 * 视觉反馈 (通过 LV_STATE_PRESSED 自动处理):
 *   - 按下: 缩放至 90% (230/256), 背景透明度 80%
 *   - 松开: 自动恢复 100% (256/256), 不透明
 *
 * BSP LED 控制 (PWM 调光):
 *   bsp_led_set(led, level)  level 取值范围因 BSP 而异:
 *   - 常见范围: 0-100 (百分比) 或 0-255 (PWM 占空比)
 *   当前设为 80, 对应 0-100 范围的 80% 亮度。
 */
#include "led_control.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"       /* bsp_led_init, bsp_led_set, bsp_led_t */

static const char *TAG = "LED_CTRL";

/* 布局常量 */
#define BTN_DIAMETER    120                         /* 按钮直径 (像素) */
#define BTN_RADIUS      60                          /* 圆角半径 (直径一半=正圆形) */
#define BTN_GAP         40                          /* 按钮间垂直间距 */
#define FIRST_BTN_Y     100                         /* 第一个按钮垂直位置 */
#define TITLE_Y         30                          /* 标题垂直位置 */

/* LED 亮度 (0-100 范围, 80 = 80%) */
/* TODO: 如果 BSP 使用 0-255 范围, 将 80 改为 204 */
#define LED_BRIGHTNESS  80

/* 按钮颜色标识, 对应 bsp_led_t 枚举 */
typedef enum {
    BTN_RED   = 0,
    BTN_GREEN = 1,
    BTN_BLUE  = 2,
} btn_color_t;

/* =============================================================
 * 设置 RGB LED 颜色
 *
 * 先熄灭所有通道 (设为 0), 再点亮目标通道。
 * 使用 BSP 官方接口, 不直接操作 GPIO。
 * ============================================================= */
static void set_rgb_led(btn_color_t color)
{
    /* 熄灭所有通道 (写入 0) */
    bsp_led_set(BSP_LED_RED,   0);
    bsp_led_set(BSP_LED_GREEN, 0);
    bsp_led_set(BSP_LED_BLUE,  0);

    /* 点亮目标通道 (80% 亮度) */
    switch (color) {
        case BTN_RED:
            bsp_led_set(BSP_LED_RED, LED_BRIGHTNESS);
            ESP_LOGD(TAG, "LED -> RED (%d%%)", LED_BRIGHTNESS);
            break;
        case BTN_GREEN:
            bsp_led_set(BSP_LED_GREEN, LED_BRIGHTNESS);
            ESP_LOGD(TAG, "LED -> GREEN (%d%%)", LED_BRIGHTNESS);
            break;
        case BTN_BLUE:
            bsp_led_set(BSP_LED_BLUE, LED_BRIGHTNESS);
            ESP_LOGD(TAG, "LED -> BLUE (%d%%)", LED_BRIGHTNESS);
            break;
        default:
            break;
    }
}

/* =============================================================
 * 按钮事件回调
 *
 * 视觉反馈通过 LV_STATE_PRESSED 样式自动处理,
 * 回调中只需要处理点击动作。
 * ============================================================= */
static void btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_SHORT_CLICKED) {
        return;     /* 只处理短点击事件 */
    }

    lv_obj_t *btn = lv_event_get_target(e);
    btn_color_t color = (btn_color_t)(intptr_t)lv_obj_get_user_data(btn);
    set_rgb_led(color);
}

/* =============================================================
 * 创建单个圆形控制按钮
 *
 * 使用 LVGL 状态样式实现按下效果:
 *   LV_STATE_DEFAULT -> 100% 大小, 不透明
 *   LV_STATE_PRESSED -> 90% 缩放, 80% 透明度
 * ============================================================= */
static lv_obj_t *create_circle_btn(lv_obj_t *parent, btn_color_t color,
                                   uint32_t bg_hex, const char *label_text,
                                   lv_coord_t y_pos)
{
    /* 创建按钮基体 */
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);                        /* 清除默认样式 */
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);         /* 可点击 */
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);      /* 不可滚动 */
    lv_obj_set_size(btn, BTN_DIAMETER, BTN_DIAMETER);    /* 120x120 */

    /* --- 圆形外观 --- */
    lv_obj_set_style_radius(btn, BTN_RADIUS, LV_STATE_DEFAULT);   /* 60px = 正圆形 */

    /* --- 水平居中在右侧区域 (400-799) --- */
    /* 右侧区域宽度 400px, 按钮直径 120px, 左边界偏移量 = (400-120)/2 = 140 */
    lv_obj_set_pos(btn, 400 + (400 - BTN_DIAMETER) / 2, y_pos);

    /* --- 默认状态样式 --- */
    /* 背景: 纯色填充 */
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_hex), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_DEFAULT);

    /* 白色边框: 3px 宽, 60% 透明度 */
    lv_obj_set_style_border_color(btn, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 3, LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(btn, LV_OPA_60, LV_STATE_DEFAULT);

    /* --- 按下状态样式 (视觉反馈) --- */
    /* 缩放至 90%: 256=100%, 230≈90% */
    lv_obj_set_style_transform_scale(btn, 230, LV_STATE_PRESSED);
    /* 缩放中心: 按钮中心 (60, 60) */
    lv_obj_set_style_transform_pivot_x(btn, BTN_RADIUS, LV_STATE_DEFAULT);
    lv_obj_set_style_transform_pivot_y(btn, BTN_RADIUS, LV_STATE_DEFAULT);
    /* 背景变暗 (80% 不透明度) */
    lv_obj_set_style_bg_opa(btn, LV_OPA_80, LV_STATE_PRESSED);

    /* --- 按钮文字标签 --- */
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label_text);
    lv_obj_center(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_STATE_DEFAULT);

    /* 存储颜色标识, 注册事件回调 */
    lv_obj_set_user_data(btn, (void *)(intptr_t)color);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_SHORT_CLICKED, NULL);

    return btn;
}

/* =============================================================
 * 公共 API: 创建 LED 控制面板
 * ============================================================= */
lv_obj_t *led_control_create(lv_obj_t *parent)
{
    ESP_LOGI(TAG, "正在创建LED控制面板...");

    /* -----------------------------------------------------------
     * 右侧面板背景 (400-799, 0-479)
     * 半透明深色背景, 不拦截触摸事件
     * ----------------------------------------------------------- */
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, 400, 480);
    lv_obj_set_pos(panel, 400, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* -----------------------------------------------------------
     * 标题: "RGB LED"
     * 居中显示在右侧面板顶部
     * ----------------------------------------------------------- */
    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "RGB LED");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    /* 标题水平居中在右侧面板, 垂直在 TITLE_Y 处 */
    lv_obj_align(title, LV_ALIGN_TOP_MID, 400, TITLE_Y);

    /* 标题下方的装饰分隔线 */
    lv_obj_t *separator = lv_obj_create(panel);
    lv_obj_set_size(separator, 200, 2);
    lv_obj_set_style_bg_color(separator, lv_color_hex(0x3A3A5C), 0);
    lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(separator, 0, 0);
    lv_obj_set_style_pad_all(separator, 0, 0);
    lv_obj_clear_flag(separator, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align_to(separator, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    /* -----------------------------------------------------------
     * 三个圆形彩色按钮 (垂直等距排列)
     *
     * Y 坐标计算:
     *   红色: FIRST_BTN_Y                       = 100
     *   绿色: 100 + 120 + 40                    = 260
     *   蓝色: 100 + 2*(120 + 40)                = 420
     * ----------------------------------------------------------- */
    create_circle_btn(panel, BTN_RED,   0xE53935, "RED",   FIRST_BTN_Y);
    create_circle_btn(panel, BTN_GREEN, 0x43A047, "GREEN", FIRST_BTN_Y + BTN_DIAMETER + BTN_GAP);
    create_circle_btn(panel, BTN_BLUE,  0x1E88E5, "BLUE",  FIRST_BTN_Y + 2 * (BTN_DIAMETER + BTN_GAP));

    /* 底部提示文字 */
    lv_obj_t *hint = lv_label_create(panel);
    lv_label_set_text(hint, "Press to control LED");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x606080), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 400, -15);

    ESP_LOGI(TAG, "LED控制面板创建完成 (3个圆形按钮)");
    return panel;
}
