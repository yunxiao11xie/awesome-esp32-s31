/**
 * @file hal_led.c
 * @brief WS2812 RGB LED 硬件封装（RMT 驱动）
 *
 * 所有 LED 状态（on/off、模式、亮度）封存在本模块内部，
 * 外部通过行为接口访问，不直接读写状态变量。
 */
#include "bsp_board.h"
#include "hal_led.h"

#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"
#include <string.h>

static const char *TAG = "hal_led";

/* ---- 硬件资源 ---- */
static bool                 s_initialized   = false;
static rmt_channel_handle_t s_rmt_channel   = NULL;
static rmt_encoder_handle_t s_encoder       = NULL;
static uint8_t              s_pixel_data[3];   /* GRB */
static SemaphoreHandle_t    s_rmt_lock      = NULL;  /* RMT 互斥锁 */

/* ---- LED 状态 ---- */
static bool     s_on         = false;
static uint8_t  s_mode       = 0;       /* 0=红, 1=绿, 2=蓝 */
static uint8_t  s_brightness = LED_BRIGHTNESS;

/* ================================================================
 *  硬件初始化
 * ================================================================ */

esp_err_t hal_led_init(void)
{
    ESP_LOGI(TAG, "初始化 WS2812 (GPIO%d)", LED_WS2812_GPIO);

    /* ---- 1. 创建 RMT TX 通道 ---- */
    rmt_tx_channel_config_t tx_chan_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = LED_WS2812_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz = 10 * 1000 * 1000,
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR(
        rmt_new_tx_channel(&tx_chan_cfg, &s_rmt_channel),
        TAG, "RMT TX 通道创建失败"
    );

    /* ---- 2. 安装 WS2812 编码器 ---- */
    led_strip_encoder_config_t encoder_cfg = {
        .resolution = 10 * 1000 * 1000,
    };
    ESP_RETURN_ON_ERROR(
        rmt_new_led_strip_encoder(&encoder_cfg, &s_encoder),
        TAG, "LED 编码器创建失败"
    );

    /* ---- 3. 启用 RMT TX 通道 ---- */
    ESP_RETURN_ON_ERROR(
        rmt_enable(s_rmt_channel),
        TAG, "RMT 通道启用失败"
    );

    s_rmt_lock = xSemaphoreCreateMutex();
    if (s_rmt_lock == NULL) {
        ESP_LOGE(TAG, "RMT 互斥锁创建失败");
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    memset(s_pixel_data, 0, sizeof(s_pixel_data));
    ESP_LOGI(TAG, "WS2812 初始化完成");
    return ESP_OK;
}

/* ================================================================
 *  硬件写入
 * ================================================================ */

static void led_hw_set_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!s_initialized) return;

    /* RMT 互斥：防止 gif 颜色追踪（LVGL 任务）与按键任务并发访问 */
    if (xSemaphoreTake(s_rmt_lock, portMAX_DELAY) != pdTRUE) return;

    /* WS2812 使用 GRB 顺序 */
    s_pixel_data[0] = green;
    s_pixel_data[1] = red;
    s_pixel_data[2] = blue;

    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
    };
    rmt_transmit(s_rmt_channel, s_encoder,
                 s_pixel_data, sizeof(s_pixel_data), &tx_cfg);
    rmt_tx_wait_all_done(s_rmt_channel, portMAX_DELAY);

    xSemaphoreGive(s_rmt_lock);
}

/* ================================================================
 *  状态 → 硬件 同步
 * ================================================================ */

static void led_apply(void)
{
    if (!s_on) {
        led_hw_set_rgb(0, 0, 0);
        return;
    }

    switch (s_mode) {
    case 0: led_hw_set_rgb(s_brightness, 0, 0);          break;
    case 1: led_hw_set_rgb(0, s_brightness, 0);          break;
    case 2: led_hw_set_rgb(0, 0, s_brightness);          break;
    default:
        s_mode = 0;
        led_hw_set_rgb(s_brightness, 0, 0);
        break;
    }
}

/* ================================================================
 *  对外接口
 * ================================================================ */

void hal_led_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    led_hw_set_rgb(r, g, b);
}

void hal_led_set_mode(uint8_t mode)
{
    s_mode = mode % 3;
    s_on = true;
    led_apply();
}

void hal_led_set_brightness(uint8_t brightness)
{
    s_brightness = brightness;
    if (s_on) led_apply();
}

void hal_led_adjust_brightness(int delta)
{
    int v = (int)s_brightness + delta;
    if (v < 0)      v = 0;
    if (v > 255)    v = 255;
    s_brightness = (uint8_t)v;
    if (s_brightness > 0) s_on = true;
    if (s_on) led_apply();
}

void hal_led_on(void)
{
    s_on = true;
    led_apply();
}

void hal_led_off(void)
{
    s_on = false;
    led_apply();
}

void hal_led_toggle(void)
{
    s_on ? hal_led_off() : hal_led_on();
}

bool hal_led_is_on(void)          { return s_on; }
uint8_t hal_led_get_mode(void)    { return s_mode; }
uint8_t hal_led_get_brightness(void) { return s_brightness; }
