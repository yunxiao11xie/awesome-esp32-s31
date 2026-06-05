/**
 * @file gif_player_main.c
 * @brief ESP32-S31-Korvo 动图播放器与 RGB LED 控制程序
 *
 * 功能说明：
 *   - 左侧 (0-399px)：循环播放 GIF 动图
 *   - 右侧 (400-799px)：三个垂直排列的圆形按钮控制 RGB LED
 *   - 触摸交互：GT1151 触控，按钮按下有缩放+变暗视觉反馈
 *
 * 硬件依赖：ESP-IDF Master + esp_lvgl_port v2.8.0 + LVGL v8.4.0
 *
 * 注意：本代码不使用板级 BSP 包，直接通过 IDF 组件驱动：
 *       esp_lcd → RGB LCD 面板
 *       esp_lcd_touch_gt1151 → GT1151 触摸
 *       ledc → RGB LED PWM
 *       esp_lvgl_port → LVGL 桥接
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"

/* ---- GPIO / I2C / LCD / LEDC 驱动 ---- */
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_io_i2c.h"

/* ---- GT1151 触摸 ---- */
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt1151.h"

/* ---- LVGL + esp_lvgl_port ---- */
#include "lvgl.h"
#include "esp_lvgl_port.h"

/* ---- LVGL GIF 动图组件（需在 menuconfig 中开启 LV_USE_GIF） ---- */
#include "extra/libs/gif/lv_gif.h"


/* ================================================================
 *  板级引脚定义
 *  ================================================================
 *  TODO: 请根据您的 ESP32-S31-Korvo 原理图填写以下引脚号。
 *        如果某引脚未使用，设为 -1。
 *
 *  指示灯：GPIO_NUM_XX 代表实际引脚号。
 *  例如 GPIO_NUM_4 代指 GPIO4。
 * ================================================================ */

/* ---------- RGB LCD 接口引脚（原理图对照） ---------- */
/* SPI 引脚（ST7262E43 配置接口，暂未使用）：LCD_CS=GPIO38, LCD_MOSI=GPIO60, LCD_SCK=GPIO61 */
#define LCD_HSYNC_GPIO      GPIO_NUM_44   /* 行同步 (原理图 LCD_H_SYNC → GPIO44) */
#define LCD_VSYNC_GPIO      GPIO_NUM_45   /* 场同步 (原理图 LCD_V_SYNC → GPIO45) */
#define LCD_DE_GPIO         GPIO_NUM_43   /* 数据使能 (原理图 LCD_H_EN → GPIO43) */
#define LCD_PCLK_GPIO       GPIO_NUM_40   /* 像素时钟 (原理图 LCD_PCLK → GPIO40) */
#define LCD_DISP_GPIO       (-1)          /* 背光/显示使能（-1=不使用） */

/* RGB565 数据线（16 位并行，按原理图接线顺序） */
/*   蓝 B3~B7 → GPIO8~12，绿 G2~G7 → GPIO13~18，红 R3~R7 → GPIO19,33~36 */
#define LCD_DATA0_GPIO      GPIO_NUM_8    /* B3 */
#define LCD_DATA1_GPIO      GPIO_NUM_9    /* B4 */
#define LCD_DATA2_GPIO      GPIO_NUM_10   /* B5 */
#define LCD_DATA3_GPIO      GPIO_NUM_11   /* B6 */
#define LCD_DATA4_GPIO      GPIO_NUM_12   /* B7 */
#define LCD_DATA5_GPIO      GPIO_NUM_13   /* G2 */
#define LCD_DATA6_GPIO      GPIO_NUM_14   /* G3 */
#define LCD_DATA7_GPIO      GPIO_NUM_15   /* G4 */
#define LCD_DATA8_GPIO      GPIO_NUM_16   /* G5 */
#define LCD_DATA9_GPIO      GPIO_NUM_17   /* G6 */
#define LCD_DATA10_GPIO     GPIO_NUM_18   /* G7 */
#define LCD_DATA11_GPIO     GPIO_NUM_19   /* R3 */
#define LCD_DATA12_GPIO     GPIO_NUM_33   /* R4 */
#define LCD_DATA13_GPIO     GPIO_NUM_34   /* R5 */
#define LCD_DATA14_GPIO     GPIO_NUM_35   /* R6 */
#define LCD_DATA15_GPIO     GPIO_NUM_36   /* R7 */

/* ---------- GT1151 触摸 I2C 引脚（原理图: LCD_I2C_SDA/SCL） ---------- */
#define TOUCH_I2C_SDA       GPIO_NUM_0    /* I2C 数据线 (LCD_I2C_SDA → GPIO0) */
#define TOUCH_I2C_SCL       GPIO_NUM_1    /* I2C 时钟线 (LCD_I2C_SCL → GPIO1) */
#define TOUCH_RST_GPIO      (-1)          /* 触摸复位（-1=不使用） */
#define TOUCH_INT_GPIO      (-1)          /* 触摸中断（-1=不使用） */

/* ---------- WS2812 RGB LED ---------- */
/* WS2812 智能灯珠，单线（GPIO37）串行控制 */
#define LED_WS2812_GPIO     GPIO_NUM_37

/* ================================================================
 *  屏幕与布局常量
 * ================================================================ */

#define LCD_H_RES           800
#define LCD_V_RES           480

/* 左侧 GIF 显示区域 */
#define GIF_AREA_X          0
#define GIF_AREA_Y          0
#define GIF_AREA_W          400
#define GIF_AREA_H          LCD_V_RES

/* 右侧面板 */
#define RIGHT_PANEL_X       400
#define RIGHT_PANEL_W       400

/* 右侧按钮控制区域（坐标相对于右侧面板） */
#define BTN_REL_CENTER_X    (RIGHT_PANEL_W / 2)   /* 面板内水平居中 = 200 */
#define BTN_DIAMETER        80            /* 按钮直径（原来120太大，挡住标题） */
#define BTN_TOP_Y           110           /* 顶部按钮 Y 中心（留出标题空间） */
#define BTN_MID_Y           (LCD_V_RES / 2)   /* 中部按钮 Y 中心 */
#define BTN_BOT_Y           (LCD_V_RES - 110) /* 底部按钮 Y 中心 */

/* LED PWM 亮度（0-255） */
#define LED_BRIGHTNESS      204           /* 约 80% */

/* 按钮缩放动画 */
#define ZOOM_NORMAL         256           /* LV_IMG_ZOOM_NONE */
#define ZOOM_PRESSED        230           /* 90% */


/* ================================================================
 *  嵌入式 GIF 二进制数据声明
 * ================================================================
 * TODO: 替换文件名以匹配您的 GIF 文件。
 * 符号命名规则：文件名 animation.gif → _binary_animation_gif_start
 */
/* CMakeLists.txt 中路径为 "gif/dance_1.gif"，链接器生成的符号名不含目录前缀 */
extern const uint8_t _binary_dance_1_gif_start[];
extern const uint8_t _binary_dance_1_gif_end[];


/* ================================================================
 *  静态变量
 * ================================================================ */

static const char *TAG = "gif_player";

/* LCD */
static esp_lcd_panel_handle_t lcd_panel = NULL;

/* 触摸 */
static esp_lcd_touch_handle_t touch_handle = NULL;

/* LVGL 显示设备 */
static lv_disp_t *lvgl_disp = NULL;

/* RGB LED 状态 */
static bool led_initialized = false;

/* WS2812 RMT 句柄 */
static rmt_channel_handle_t led_rmt_channel = NULL;
static rmt_encoder_handle_t led_encoder = NULL;
static uint8_t led_pixel_data[3];   /* GRB 格式数据缓存 */

/* 按钮对象 */
static lv_obj_t *btn_red = NULL;
static lv_obj_t *btn_green = NULL;
static lv_obj_t *btn_blue = NULL;


/* ================================================================
 *  WS2812 RGB LED 控制（基于 RMT）
 * ================================================================ */

/**
 * @brief 初始化 WS2812 RGB LED
 *
 * 使用 RMT 外设驱动 WS2812 智能灯珠（单线控制）。
 * 编码器使用 10MHz 分辨率，精确控制 WS2812 时序。
 */
static esp_err_t led_init(void)
{
    ESP_LOGI(TAG, "初始化 WS2812 RGB LED (GPIO%d)", LED_WS2812_GPIO);

    /* ---- 1. 创建 RMT TX 通道 ---- */
    rmt_tx_channel_config_t tx_chan_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = LED_WS2812_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz = 10 * 1000 * 1000,   /* 10MHz，1 tick = 0.1us */
        .trans_queue_depth = 4,
    };
    esp_err_t ret = rmt_new_tx_channel(&tx_chan_cfg, &led_rmt_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RMT TX 通道创建失败: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "RMT TX 通道 OK");

    /* ---- 2. 安装 WS2812 编码器 ---- */
    led_strip_encoder_config_t encoder_cfg = {
        .resolution = 10 * 1000 * 1000,
    };
    ret = rmt_new_led_strip_encoder(&encoder_cfg, &led_encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED 编码器创建失败: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "WS2812 编码器 OK");

    /* ---- 3. 启用 RMT TX 通道 ---- */
    ret = rmt_enable(led_rmt_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RMT 通道启用失败: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "RMT 通道已启用");

    /* 初始状态：LED 关闭 */
    memset(led_pixel_data, 0, sizeof(led_pixel_data));

    led_initialized = true;
    ESP_LOGI(TAG, "WS2812 RGB LED 初始化完成");
    return ESP_OK;
}

/**
 * @brief 设置 WS2812 颜色
 *
 * @param red   红色 0-255
 * @param green 绿色 0-255
 * @param blue  蓝色 0-255
 */
static void led_set_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!led_initialized) return;

    /* WS2812 使用 GRB 顺序 */
    led_pixel_data[0] = green;
    led_pixel_data[1] = red;
    led_pixel_data[2] = blue;

    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
    };
    rmt_transmit(led_rmt_channel, led_encoder,
                 led_pixel_data, sizeof(led_pixel_data), &tx_cfg);
    rmt_tx_wait_all_done(led_rmt_channel, portMAX_DELAY);
}


/* ================================================================
 *  LCD 初始化（RGB 并行接口）
 * ================================================================ */

/**
 * @brief 初始化 RGB 并行 LCD 面板
 *
 * 使用 esp_lcd 组件创建 RGB565 接口面板。
 * 帧缓冲区和 LVGL 显示缓冲区分均配到 PSRAM。
 *
 * @return ESP_OK 成功，否则失败
 */
static esp_err_t lcd_init(void)
{
    /* 拼接数据引脚数组 */
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
            .pclk_hz = 18 * 1000 * 1000,          /* 18 MHz 像素时钟 */
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_pulse_width = 40,               /* 水平同步脉宽 */
            .hsync_back_porch = 40,                /* 水平后廊 */
            .hsync_front_porch = 48,               /* 水平前廊 */
            .vsync_pulse_width = 23,               /* 垂直同步脉宽 */
            .vsync_back_porch = 32,                /* 垂直后廊 */
            .vsync_front_porch = 13,               /* 垂直前廊 */
            .flags = {
                .pclk_active_neg = true,           /* 像素时钟下降沿采样 */
            },
        },
        .data_width = 16,                          /* RGB565 */
        .in_color_format = LCD_COLOR_FMT_RGB565,   /* 输入颜色格式 */
        .out_color_format = LCD_COLOR_FMT_RGB565,  /* 输出颜色格式 */
        .num_fbs = 2,                              /* 双帧缓冲 */
        .bounce_buffer_size_px = 0,                /* 不使用回弹缓冲 */
        .dma_burst_size = 64,                      /* DMA 突发大小 */
        .hsync_gpio_num = LCD_HSYNC_GPIO,
        .vsync_gpio_num = LCD_VSYNC_GPIO,
        .de_gpio_num = LCD_DE_GPIO,
        .pclk_gpio_num = LCD_PCLK_GPIO,
        .disp_gpio_num = LCD_DISP_GPIO,            /* -1 = 不使用 */
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
            .fb_in_psram = true,                   /* 帧缓冲放到 PSRAM */
        },
    };

    ESP_RETURN_ON_ERROR(
        esp_lcd_new_rgb_panel(&rgb_cfg, &lcd_panel),
        TAG, "RGB 面板创建失败"
    );

    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_reset(lcd_panel),
        TAG, "面板复位失败"
    );

    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_init(lcd_panel),
        TAG, "面板初始化失败"
    );

    /* 关闭显示（LVGL 准备好后再开启） */
    esp_lcd_panel_disp_on_off(lcd_panel, false);

    ESP_LOGI(TAG, "LCD 初始化完成 (%dx%d RGB565)", LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}


/* ================================================================
 *  触摸初始化（GT1151 over I2C）
 * ================================================================ */

/**
 * @brief 初始化 I2C 总线并创建 GT1151 触摸设备
 *
 * @return ESP_OK 成功，否则失败
 */
static esp_err_t touch_init(void)
{
    /* ---- 1. 创建 I2C 主总线 ---- */
    i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port = -1,                              /* -1 = 自动分配端口 */
        .sda_io_num = TOUCH_I2C_SDA,
        .scl_io_num = TOUCH_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,           /* 启用内部上拉 */
        },
    };

    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_RETURN_ON_ERROR(
        i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus),
        TAG, "I2C 总线创建失败"
    );

    /* ---- 2. 配置 GT1151 触摸参数 ---- */
    esp_lcd_touch_config_t touch_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = TOUCH_RST_GPIO,               /* -1 = 不使用 */
        .int_gpio_num = TOUCH_INT_GPIO,               /* -1 = 不使用 */
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

    /* ---- 3. 创建 I2C 面板 IO 句柄 ---- */
    esp_lcd_panel_io_handle_t gt1151_io = NULL;
    const esp_lcd_panel_io_i2c_config_t gt1151_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT1151_CONFIG();
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i2c(i2c_bus, &gt1151_io_cfg, &gt1151_io),
        TAG, "GT1151 I2C IO 创建失败"
    );

    /* ---- 4. 创建 GT1151 触摸设备 ---- */
    ESP_RETURN_ON_ERROR(
        esp_lcd_touch_new_i2c_gt1151(gt1151_io, &touch_cfg, &touch_handle),
        TAG, "GT1151 触摸初始化失败"
    );

    ESP_LOGI(TAG, "GT1151 触摸初始化完成");
    return ESP_OK;
}


/* ================================================================
 *  LVGL 初始化（通过 esp_lvgl_port）
 * ================================================================ */

/**
 * @brief 初始化 LVGL 并注册 LCD 显示设备和触摸设备
 *
 * 步骤：
 *   1. 调用 lvgl_port_init() 创建 LVGL 主任务
 *   2. 调用 lvgl_port_add_display() 注册 RGB LCD 面板
 *   3. 调用 lvgl_port_add_touch() 注册 GT1151 触摸
 *
 * @return ESP_OK 成功，否则失败
 */
static esp_err_t lvgl_init(void)
{
    /* ---- 1. 配置并初始化 LVGL 端口 ---- */
    /*
     * TODO: 如果编译报错提示 lvgl_port_cfg_t 字段不匹配，
     * 请参考当前 managed_components 中 esp_lvgl_port 的头文件调整。
     */
    lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,          /* 优先级 4 */
        .task_stack = 8192,          /* 栈大小 8192 */
        .task_affinity = 0,          /* CPU0 */
        .task_max_sleep_ms = 10,     /* 最大休眠 10ms */
        .timer_period_ms = 5,        /* LVGL 定时器周期 5ms */
    };

    ESP_RETURN_ON_ERROR(
        lvgl_port_init(&lvgl_cfg),
        TAG, "LVGL 端口初始化失败"
    );

    /* ---- 2. 将 LCD 面板注册为 LVGL 显示设备 ---- */
    /*
     * TODO: 如果 lvgl_port_display_cfg_t 字段名不同（如 draw_buffer、flush_cb 等），
     * 请根据 managed_components/espressif__esp_lvgl_port/include/esp_lvgl_port.h 调整。
     */
    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = NULL,                       /* RGB 面板不需要 IO 句柄 */
        .panel_handle = lcd_panel,
        .buffer_size = LCD_H_RES * 100,          /* 缓冲区大小 */
        .double_buffer = true,                   /* 双缓冲 */
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .flags = {
            .buff_dma = true,                    /* 支持 DMA */
            .buff_spiram = true,                 /* 分配到 PSRAM */
        },
    };

    /* RGB 显示屏专用配置 */
    lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = 0,
            .avoid_tearing = 0,
        },
    };

    lvgl_disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (lvgl_disp == NULL) {
        ESP_LOGE(TAG, "LVGL 显示设备添加失败");
        return ESP_FAIL;
    }

    /* ---- 3. 将 GT1151 触摸注册为 LVGL 输入设备 ---- */
    if (touch_handle != NULL) {
        lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lvgl_disp,
            .handle = touch_handle,
        };

        lv_indev_t *touch_indev = lvgl_port_add_touch(&touch_cfg);
        if (touch_indev == NULL) {
            ESP_LOGW(TAG, "触摸设备添加失败，触摸不可用");
            /* 触摸失败不影响显示核心功能，不做致命处理 */
        }
    } else {
        ESP_LOGW(TAG, "触摸未初始化，跳过触摸注册");
    }

    /* 开启 LCD 显示 */
    esp_lcd_panel_disp_on_off(lcd_panel, true);

    /* 设置屏幕背景色为深灰蓝 */
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_make(34, 38, 46), 0);

    ESP_LOGI(TAG, "LVGL 初始化完成: 显示+触摸已注册");
    return ESP_OK;
}


/* ================================================================
 *  按钮事件回调
 * ================================================================ */

/**
 * @brief 按钮事件处理
 *
 * - PRESSED:    缩放至 90%（背景色自动由 LV_STATE_PRESSED 处理）
 * - RELEASED:   恢复 100%
 * - CLICKED:    切换 RGB LED 颜色
 */
static void btn_event_cb(lv_event_t *evt)
{
    lv_event_code_t code = lv_event_get_code(evt);
    lv_obj_t *btn = lv_event_get_target(evt);

    /* 从 user_data 取出颜色标记：0=红，1=绿，2=蓝 */
    uint32_t color_idx = (uint32_t)(uintptr_t)lv_obj_get_user_data(btn);

    if (code == LV_EVENT_PRESSED) {
        lv_obj_set_style_transform_zoom(btn, ZOOM_PRESSED, 0);

    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_obj_set_style_transform_zoom(btn, ZOOM_NORMAL, 0);

    } else if (code == LV_EVENT_CLICKED) {
        if (!led_initialized) return;

        /* 根据颜色索引设置对应 PWM 亮度 */
        switch (color_idx) {
        case 0: /* 红色 */
            led_set_rgb(LED_BRIGHTNESS, 0, 0);
            ESP_LOGI(TAG, "LED → 红色 (R=%d, G=%d, B=%d)",
                     LED_BRIGHTNESS, 0, 0);
            break;
        case 1: /* 绿色 */
            led_set_rgb(0, LED_BRIGHTNESS, 0);
            ESP_LOGI(TAG, "LED → 绿色 (R=%d, G=%d, B=%d)",
                     0, LED_BRIGHTNESS, 0);
            break;
        case 2: /* 蓝色 */
            led_set_rgb(0, 0, LED_BRIGHTNESS);
            ESP_LOGI(TAG, "LED → 蓝色 (R=%d, G=%d, B=%d)",
                     0, 0, LED_BRIGHTNESS);
            break;
        default:
            break;
        }
    }
}


/* ================================================================
 *  GIF 动图创建
 * ================================================================ */

/**
 * @brief 在左侧区域创建并播放 GIF 动图
 *
 * 从 Flash 嵌入的二进制读取 GIF 数据，复制到 PSRAM 后创建 lv_gif 对象。
 * 使用裁剪容器确保动图不超出左侧 400px 范围。
 *
 * @return ESP_OK 成功，否则失败
 */
static esp_err_t create_gif_animation(void)
{
    size_t gif_size = (size_t)(_binary_dance_1_gif_end - _binary_dance_1_gif_start);

    if (gif_size == 0) {
        ESP_LOGE(TAG, "GIF 文件为空！请检查 CMakeLists.txt 中的 target_add_binary_data 路径");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "GIF 文件大小: %zu bytes", gif_size);

    /* ---- 在 PSRAM 中分配 GIF 数据缓冲区 ---- */
    uint8_t *gif_buffer = (uint8_t *)heap_caps_malloc(gif_size, MALLOC_CAP_SPIRAM);
    if (gif_buffer == NULL) {
        ESP_LOGE(TAG, "PSRAM 分配失败 (%zu bytes)，请确认 PSRAM 已启用", gif_size);
        return ESP_ERR_NO_MEM;
    }
    memcpy(gif_buffer, _binary_dance_1_gif_start, gif_size);
    ESP_LOGI(TAG, "GIF 数据已复制到 PSRAM: %zu bytes", gif_size);

    /* ---- 使用静态图像描述符（内部 RAM，更稳定） ---- */
    static lv_img_dsc_t gif_img_dsc;
    memset(&gif_img_dsc, 0, sizeof(lv_img_dsc_t));
    gif_img_dsc.header.cf = LV_IMG_CF_RAW;          /* 原始编码数据 */
    gif_img_dsc.data = gif_buffer;
    gif_img_dsc.data_size = gif_size;
    /* w/h 由 GIF 解码器自动填充 */
    ESP_LOGI(TAG, "图像描述符已创建: cf=%d, data_size=%zu", gif_img_dsc.header.cf, gif_img_dsc.data_size);

    /* ---- 创建左侧区域裁剪容器 ---- */
    lv_obj_t *cont = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(cont, GIF_AREA_X, GIF_AREA_Y);
    lv_obj_set_size(cont, GIF_AREA_W, GIF_AREA_H);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_set_style_clip_corner(cont, true, 0);       /* 裁剪超出部分 */
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    ESP_LOGI(TAG, "左侧容器已创建 (%dx%d)", GIF_AREA_W, GIF_AREA_H);

    /* ---- 创建 GIF 动图对象 ---- */
    lv_obj_t *gif_obj = lv_gif_create(cont);
    if (gif_obj == NULL) {
        ESP_LOGE(TAG, "lv_gif_create 返回 NULL");
        heap_caps_free(gif_buffer);
        lv_obj_del(cont);
        return ESP_FAIL;
    }

    lv_gif_set_src(gif_obj, &gif_img_dsc);
    lv_obj_center(gif_obj);                            /* 容器内居中 */
    lv_img_set_zoom(gif_obj, ZOOM_NORMAL);

    ESP_LOGI(TAG, "GIF 动图已创建，自动播放中");
    return ESP_OK;
}


/* ================================================================
 *  圆形按钮创建
 * ================================================================ */

/**
 * @brief 创建一个圆形控制按钮
 *
 * @param color      填充色
 * @param color_dim  按下态填充色
 * @param cx         圆心 x
 * @param cy         圆心 y
 * @param idx        颜色索引（0=红,1=绿,2=蓝），存入 user_data
 * @return lv_obj_t*  按钮对象，失败返回 NULL
 */
static lv_obj_t *create_round_button(lv_color_t color, lv_color_t color_dim,
                                      lv_coord_t cx, lv_coord_t cy,
                                      uint32_t idx, lv_obj_t *parent)
{
    lv_obj_t *btn = lv_btn_create(parent ? parent : lv_scr_act());
    if (btn == NULL) return NULL;

    lv_obj_set_size(btn, BTN_DIAMETER, BTN_DIAMETER);
    lv_obj_set_pos(btn, cx - BTN_DIAMETER / 2, cy - BTN_DIAMETER / 2);

    /* ---- 圆形样式 ---- */
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

    /* 白色边框 */
    lv_obj_set_style_border_color(btn, lv_color_white(), 0);
    lv_obj_set_style_border_width(btn, 3, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);

    /* 按下态背景色（LVGL 状态机自动切换） */
    lv_obj_set_style_bg_color(btn, color_dim, LV_STATE_PRESSED);

    /* 阴影效果 */
    lv_obj_set_style_shadow_width(btn, 8, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_white(), 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_30, 0);
    lv_obj_set_style_shadow_ofs_x(btn, 0, 0);
    lv_obj_set_style_shadow_ofs_y(btn, 0, 0);

    /* 存储颜色索引 */
    lv_obj_set_user_data(btn, (void *)(uintptr_t)idx);

    /* ---- 注册事件 ---- */
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);

    return btn;
}


/* ================================================================
 *  控制面板创建
 * ================================================================ */

/**
 * @brief 创建右侧控制面板：标题 + 红/绿/蓝三个按钮
 *
 * @return ESP_OK 成功，否则失败
 */
static esp_err_t create_control_panel(void)
{
    /* ---- 右侧白色背景面板 ---- */
    lv_obj_t *right_panel = lv_obj_create(lv_scr_act());
    if (right_panel == NULL) return ESP_FAIL;

    lv_obj_set_pos(right_panel, RIGHT_PANEL_X, 0);
    lv_obj_set_size(right_panel, RIGHT_PANEL_W, LCD_V_RES);
    lv_obj_set_style_bg_color(right_panel, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(right_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(right_panel, 0, 0);
    lv_obj_set_style_radius(right_panel, 0, 0);
    lv_obj_set_style_pad_all(right_panel, 0, 0);
    lv_obj_clear_flag(right_panel, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- 标题 "RGB LED" ---- */
    lv_obj_t *title = lv_label_create(right_panel);
    if (title == NULL) return ESP_FAIL;

    lv_label_set_text(title, "RGB LED");
    lv_obj_set_style_text_color(title, lv_color_make(51, 51, 51), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    /* ---- 红色按钮（索引 0） ---- */
    btn_red = create_round_button(
        lv_color_make(255, 0, 0),      /* 纯红 */
        lv_color_make(102, 0, 0),       /* 暗红 ~40% */
        BTN_REL_CENTER_X, BTN_TOP_Y, 0, right_panel
    );
    if (btn_red == NULL) return ESP_FAIL;

    /* ---- 绿色按钮（索引 1） ---- */
    btn_green = create_round_button(
        lv_color_make(0, 255, 0),       /* 纯绿 */
        lv_color_make(0, 102, 0),       /* 暗绿 ~40% */
        BTN_REL_CENTER_X, BTN_MID_Y, 1, right_panel
    );
    if (btn_green == NULL) return ESP_FAIL;

    /* ---- 蓝色按钮（索引 2） ---- */
    btn_blue = create_round_button(
        lv_color_make(0, 0, 255),       /* 纯蓝 */
        lv_color_make(0, 0, 102),       /* 暗蓝 ~40% */
        BTN_REL_CENTER_X, BTN_BOT_Y, 2, right_panel
    );
    if (btn_blue == NULL) return ESP_FAIL;

    ESP_LOGI(TAG, "控制面板创建完成：3 个 RGB 按钮");
    return ESP_OK;
}


/* ================================================================
 *  主函数
 * ================================================================ */

/**
 * @brief 应用程序入口
 *
 * 初始化流程：
 *   1. LCD 面板初始化（RGB 并行接口）
 *   2. GT1151 触摸初始化（I2C）
 *   3. LVGL 初始化（含显示+触摸注册，自动创建 LVGL 主任务）
 *   4. RGB LED 初始化（LEDC PWM）
 *   5. 创建 UI（GIF 动图 + 控制按钮）
 */
void app_main(void)
{
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  ESP32-S31-Korvo 动图播放器 + RGB LED");
    ESP_LOGI(TAG, "  屏幕: %dx%d RGB565", LCD_H_RES, LCD_V_RES);
    ESP_LOGI(TAG, "============================================");

    /* ---- 1. LCD ---- */
    ESP_ERROR_CHECK(lcd_init());

    /* ---- 2. 触摸 ---- */
    esp_err_t touch_err = touch_init();
    if (touch_err != ESP_OK) {
        ESP_LOGW(TAG, "触摸初始化失败，继续执行（仅无触摸交互）");
    }

    /* ---- 3. LVGL ---- */
    ESP_ERROR_CHECK(lvgl_init());

    /* ---- 4. RGB LED ---- */
    esp_err_t led_err = led_init();
    if (led_err != ESP_OK) {
        ESP_LOGW(TAG, "LED 初始化跳过（按钮仅提供视觉反馈）: %s",
                 esp_err_to_name(led_err));
    }

    /* ---- 5. UI ---- */

    /* 5a. GIF 动图 */
    esp_err_t gif_err = create_gif_animation();
    if (gif_err != ESP_OK) {
        ESP_LOGW(TAG, "GIF 创建失败，左侧留空");
        lv_obj_t *err_label = lv_label_create(lv_scr_act());
        lv_label_set_text(err_label, "GIF Load Error\nCheck file path");
        lv_obj_set_pos(err_label, 50, 200);
        lv_obj_set_style_text_color(err_label, lv_color_make(255, 100, 100), 0);
    }

    /* 5b. 按钮面板 */
    ESP_ERROR_CHECK(create_control_panel());

    ESP_LOGI(TAG, "全部初始化完成！LVGL 任务已在 CPU0 运行");
}
