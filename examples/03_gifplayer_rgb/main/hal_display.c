/**
 * @file hal_display.c
 * @brief RGB LCD + GT1151 触摸初始化
 *
 * 两个设备共享同一条 I2C 总线。I2C 总线句柄作为模块内部
 * static 变量在 lcd_init 和 touch_init 之间传递。
 */
#include "bsp_board.h"
#include "hal_display.h"

#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_touch_gt1151.h"

static const char *TAG = "hal_display";

/* I2C 总线句柄，在 LCD init 中创建，被 touch init 复用 */
static i2c_master_bus_handle_t s_i2c_bus = NULL;

/* ================================================================
 *  LCD 初始化（RGB 并行接口）
 * ================================================================ */

esp_err_t hal_display_lcd_init(esp_lcd_panel_handle_t *out_panel)
{
    ESP_LOGI(TAG, "初始化 RGB LCD (%dx%d)", LCD_H_RES, LCD_V_RES);

    const gpio_num_t lcd_data_gpios[16] = {
        LCD_DATA0_GPIO,  LCD_DATA1_GPIO,  LCD_DATA2_GPIO,  LCD_DATA3_GPIO,
        LCD_DATA4_GPIO,  LCD_DATA5_GPIO,  LCD_DATA6_GPIO,  LCD_DATA7_GPIO,
        LCD_DATA8_GPIO,  LCD_DATA9_GPIO,  LCD_DATA10_GPIO, LCD_DATA11_GPIO,
        LCD_DATA12_GPIO, LCD_DATA13_GPIO, LCD_DATA14_GPIO, LCD_DATA15_GPIO,
    };

    /* RGB 面板配置（时序参考 ST7262E43 规格 @~35Hz） */
    esp_lcd_rgb_panel_config_t rgb_cfg = {
        .clk_src = LCD_CLK_SRC_PLL160M,
        .timings = {
            .pclk_hz = 18 * 1000 * 1000,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_pulse_width = 40,
            .hsync_back_porch = 40,
            .hsync_front_porch = 48,
            .vsync_pulse_width = 23,
            .vsync_back_porch = 32,
            .vsync_front_porch = 13,
            .flags = {
                .pclk_active_neg = true,
            },
        },
        .data_width = 16,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 2,
        .bounce_buffer_size_px = 0,
        .dma_burst_size = 64,
        .hsync_gpio_num = LCD_HSYNC_GPIO,
        .vsync_gpio_num = LCD_VSYNC_GPIO,
        .de_gpio_num = LCD_DE_GPIO,
        .pclk_gpio_num = LCD_PCLK_GPIO,
        .disp_gpio_num = LCD_DISP_GPIO,
        .data_gpio_nums = {
            lcd_data_gpios[0],  lcd_data_gpios[1],
            lcd_data_gpios[2],  lcd_data_gpios[3],
            lcd_data_gpios[4],  lcd_data_gpios[5],
            lcd_data_gpios[6],  lcd_data_gpios[7],
            lcd_data_gpios[8],  lcd_data_gpios[9],
            lcd_data_gpios[10], lcd_data_gpios[11],
            lcd_data_gpios[12], lcd_data_gpios[13],
            lcd_data_gpios[14], lcd_data_gpios[15],
        },
        .flags = {
            .fb_in_psram = true,
        },
    };

    ESP_RETURN_ON_ERROR(
        esp_lcd_new_rgb_panel(&rgb_cfg, out_panel),
        TAG, "RGB 面板创建失败"
    );

    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_reset(*out_panel),
        TAG, "面板复位失败"
    );

    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_init(*out_panel),
        TAG, "面板初始化失败"
    );

    /* 关闭显示 —— app_ui 就绪后再开启 */
    esp_lcd_panel_disp_on_off(*out_panel, false);

    ESP_LOGI(TAG, "LCD 初始化完成 (%dx%d RGB565)", LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}

/* ================================================================
 *  触摸初始化（GT1151 over I2C）
 * ================================================================ */

esp_err_t hal_display_touch_init(esp_lcd_touch_handle_t *out_touch)
{
    /* ---- 1. 创建 I2C 主总线 ---- */
    i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port = -1,
        .sda_io_num = TOUCH_I2C_SDA,
        .scl_io_num = TOUCH_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    ESP_RETURN_ON_ERROR(
        i2c_new_master_bus(&i2c_bus_cfg, &s_i2c_bus),
        TAG, "I2C 总线创建失败"
    );

    /* ---- 2. 配置 GT1151 触摸参数 ---- */
    esp_lcd_touch_config_t touch_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = TOUCH_RST_GPIO,
        .int_gpio_num = TOUCH_INT_GPIO,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    /* ---- 3. 创建 GT1151 设备 ---- */
    const esp_lcd_panel_io_i2c_config_t gt1151_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT1151_CONFIG();
    esp_lcd_panel_io_handle_t gt1151_io = NULL;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i2c(s_i2c_bus, &gt1151_io_cfg, &gt1151_io),
        TAG, "GT1151 I2C IO 创建失败"
    );

    ESP_RETURN_ON_ERROR(
        esp_lcd_touch_new_i2c_gt1151(gt1151_io, &touch_cfg, out_touch),
        TAG, "GT1151 触摸初始化失败"
    );

    ESP_LOGI(TAG, "GT1151 触摸初始化完成 (I2C: SDA=%d, SCL=%d)",
             TOUCH_I2C_SDA, TOUCH_I2C_SCL);
    return ESP_OK;
}
