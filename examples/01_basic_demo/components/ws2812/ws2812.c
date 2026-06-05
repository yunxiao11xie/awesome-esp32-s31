/*
 * WS2812 RGB LED 驱动 (RMT 直接驱动)
 * GPIO37, 单颗 LED
 */

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "ws2812.h"

static const char *TAG = "WS2812";
#define WS2812_PIN    GPIO_NUM_37
#define LED_COUNT     1

/* WS2812 RMT 时序 (10MHz = 0.1us/tick) */
static const rmt_symbol_word_t s_bit0  = { .duration0 = 4,  .level0 = 1, .duration1 = 9,  .level1 = 0 };
static const rmt_symbol_word_t s_bit1  = { .duration0 = 8,  .level0 = 1, .duration1 = 5,  .level1 = 0 };
static const rmt_symbol_word_t s_reset = { .duration0 = 600, .level0 = 0, .duration1 = 0, .level1 = 0 };

static rmt_channel_handle_t rmt_tx  = NULL;
static rmt_encoder_handle_t rmt_enc = NULL;

/* ==================== RMT 编码器回调 ==================== */
static size_t IRAM_ATTR ws2812_encode_cb(const void *data, size_t len,
                                          size_t written, size_t free_slots,
                                          rmt_symbol_word_t *out, bool *done, void *arg)
{
    const uint8_t *b = (const uint8_t *)data;
    size_t syms = len * 8, total = syms + 1, n = 0;

    if (written < syms) {
        while (n < free_slots && (written + n) < syms) {
            size_t bi = (written + n) / 8;
            uint8_t bp = 7 - ((written + n) % 8);
            out[n] = (b[bi] >> bp) & 1 ? s_bit1 : s_bit0;
            n++;
        }
    } else if (written < total && free_slots > 0) {
        out[0] = s_reset; n = 1;
    }
    if ((written + n) >= total) *done = true;
    return n;
}

/* ==================== 公共 API ==================== */
esp_err_t ws2812_init(void)
{
    ESP_LOGI(TAG, "Init WS2812 on GPIO%d", WS2812_PIN);

    rmt_tx_channel_config_t tx = {
        .gpio_num = WS2812_PIN, .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .flags = { .invert_out = false, .with_dma = false },
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx, &rmt_tx));

    rmt_simple_encoder_config_t enc = {
        .callback = ws2812_encode_cb, .min_chunk_size = 64,
    };
    ESP_ERROR_CHECK(rmt_new_simple_encoder(&enc, &rmt_enc));
    ESP_ERROR_CHECK(rmt_enable(rmt_tx));

    /* 熄灭 LED */
    uint8_t off[3] = {0, 0, 0};
    rmt_transmit_config_t tc = { .loop_count = 0 };
    rmt_transmit(rmt_tx, rmt_enc, off, 3, &tc);
    rmt_tx_wait_all_done(rmt_tx, pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "WS2812 ready");
    return ESP_OK;
}

esp_err_t ws2812_set_color_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t grb[3] = {g, r, b};  /* WS2812 格式: GRB */
    rmt_transmit_config_t tc = { .loop_count = 0, .flags.eot_level = 0 };
    ESP_ERROR_CHECK(rmt_transmit(rmt_tx, rmt_enc, grb, 3, &tc));
    return rmt_tx_wait_all_done(rmt_tx, pdMS_TO_TICKS(100));
}

/* ==================== 呼吸灯任务 ==================== */
static void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v,
                       uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (s == 0) { *r = *g = *b = v; return; }
    uint8_t reg = h / 60, rem = (h - reg * 60) * 255 / 60;
    uint8_t p = (v * (255 - s)) >> 8;
    uint8_t q = (v * (255 - ((s * rem) >> 8))) >> 8;
    uint8_t t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8;
    switch (reg) {
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        default:*r = v; *g = p; *b = q; break;
    }
}

static void breath_task(void *arg)
{
    uint16_t hue = 0;
    const int steps = 256, delay_ms = 20;
    while (1) {
        for (int i = 0; i < steps; i++) {
            float angle = 2.0f * M_PI * i / steps;
            uint8_t v = (uint8_t)(((sinf(angle) + 1.0f) / 2.0f) * 255.0f);
            uint8_t r, g, b;
            hsv_to_rgb(hue, 255, v, &r, &g, &b);
            ws2812_set_color_rgb(r, g, b);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
        hue = (hue + 2) % 360;
    }
}

esp_err_t ws2812_breath_start(void)
{
    if (xTaskCreate(breath_task, "breath", 2048, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create breath task");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Breath task started");
    return ESP_OK;
}
