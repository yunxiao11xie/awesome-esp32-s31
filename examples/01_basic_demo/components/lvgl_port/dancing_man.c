/*
 * 跳舞小人: LVGL canvas 直接操作 RGB565 buffer 绘制
 * 四肢用正弦波摆动, 每 50ms 重绘一帧
 */
#include <math.h>
#include <string.h>
#include "lvgl.h"
#include "dancing_man.h"

#define W  80
#define H  80
#define CX 40
#define HEAD_R 10
#define BODY_L 20

static lv_color_t canvas_buf[W * H];

/* 在 RGB565 buffer 上画线 */
static void draw_line_buf(lv_color_t *buf, int x1, int y1, int x2, int y2,
                           lv_color_t c)
{
    int dx = abs(x2 - x1), dy = -abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
    int err = dx + dy, e2;
    while (1) {
        if (x1 >= 0 && x1 < W && y1 >= 0 && y1 < H)
            buf[y1 * W + x1] = c;
        if (x1 == x2 && y1 == y2) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

/* 画实心圆 */
static void draw_circle_buf(lv_color_t *buf, int cx, int cy, int r, lv_color_t c)
{
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x*x + y*y <= r*r) {
                int px = cx + x, py = cy + y;
                if (px >= 0 && px < W && py >= 0 && py < H)
                    buf[py * W + px] = c;
            }
}

static void dance_timer_cb(lv_timer_t *t)
{
    lv_obj_t *cv = lv_timer_get_user_data(t);
    static float phase = 0.0f;
    phase += 0.25f;

    /* 清屏 */
    memset(canvas_buf, 0, sizeof(canvas_buf));
    lv_color_t white = lv_color_hex(0xFFFFFF);
    lv_color_t cyan  = lv_color_hex(0x00FFFF);

    /* 头 */
    int head_y = 15;
    draw_circle_buf(canvas_buf, CX, head_y, HEAD_R, white);

    /* 身体 */
    int bt = head_y + HEAD_R, bb = bt + BODY_L;
    draw_line_buf(canvas_buf, CX, bt, CX, bb, white);

    /* 左臂 */
    float la = sinf(phase) * 0.6f;
    int lax = CX + (int)(-18 * cosf(la)), lay = bt + 5 + (int)(10 * sinf(la));
    draw_line_buf(canvas_buf, CX - 8, bt + 5, lax, lay, cyan);

    /* 右臂 */
    float ra = sinf(phase + 1.5f) * 0.6f;
    int rax = CX + (int)(18 * cosf(ra)), ray = bt + 5 + (int)(10 * sinf(ra));
    draw_line_buf(canvas_buf, CX + 8, bt + 5, rax, ray, cyan);

    /* 左腿 */
    float ll = sinf(phase + 0.8f) * 0.5f;
    draw_line_buf(canvas_buf, CX - 5, bb, CX + (int)(-10*sinf(ll)), bb + 14, white);

    /* 右腿 */
    float rl = sinf(phase + 2.3f) * 0.5f;
    draw_line_buf(canvas_buf, CX + 5, bb, CX + (int)(10*sinf(rl)), bb + 14, white);

    /* 通知 LVGL 刷新 */
    lv_obj_invalidate(cv);
}

lv_obj_t* dancing_man_create(lv_obj_t *parent)
{
    lv_obj_t *cv = lv_canvas_create(parent);
    lv_canvas_set_buffer(cv, canvas_buf, W, H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(cv, 0, 0);
    lv_obj_set_size(cv, W, H);
    memset(canvas_buf, 0, sizeof(canvas_buf));
    lv_timer_create(dance_timer_cb, 50, cv);
    return cv;
}
