/**
 * @file bsp_board.h
 * @brief ESP32-S31-Korvo 板级硬件配置（纯宏头文件，无函数）
 *
 * 所有引脚定义、屏幕常量、布局参数、嵌入式数据声明集中于此。
 * 各模块通过 #include "bsp_board.h" 获取硬件描述。
 */
#pragma once

#include <stdint.h>
#include "hal/gpio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  RGB LCD 接口引脚（ST7262E43, RGB565 并行）
 * ================================================================ */
#define LCD_HSYNC_GPIO      GPIO_NUM_44   /* 行同步 */
#define LCD_VSYNC_GPIO      GPIO_NUM_45   /* 场同步 */
#define LCD_DE_GPIO         GPIO_NUM_43   /* 数据使能 */
#define LCD_PCLK_GPIO       GPIO_NUM_40   /* 像素时钟 */
#define LCD_DISP_GPIO       (-1)          /* 背光/显示使能（-1=不使用） */

/* RGB565 数据线（16 位并行，按原理图接线顺序） */
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

/* ================================================================
 *  GT1151 触摸 I2C 引脚
 * ================================================================ */
#define TOUCH_I2C_SDA       GPIO_NUM_0    /* I2C 数据线 */
#define TOUCH_I2C_SCL       GPIO_NUM_1    /* I2C 时钟线 */
#define TOUCH_RST_GPIO      (-1)          /* 触摸复位（-1=不使用） */
#define TOUCH_INT_GPIO      (-1)          /* 触摸中断（-1=不使用） */

/* ================================================================
 *  WS2812 RGB LED
 * ================================================================ */
#define LED_WS2812_GPIO     GPIO_NUM_37

/* ================================================================
 *  ADC 按键阵列
 * ================================================================ */
#define KEY_ARRAY_ADC_GPIO  GPIO_NUM_42

/* ================================================================
 *  屏幕分辨率
 * ================================================================ */
#define LCD_H_RES           800
#define LCD_V_RES           480

/* ================================================================
 *  左侧 GIF 显示区域
 * ================================================================ */
#define GIF_AREA_X          0
#define GIF_AREA_Y          0
#define GIF_AREA_W          400
#define GIF_AREA_H          LCD_V_RES

/* ================================================================
 *  右侧控制面板
 * ================================================================ */
#define RIGHT_PANEL_X       400
#define RIGHT_PANEL_W       400

/* ================================================================
 *  右侧按钮布局
 * ================================================================ */
#define BTN_REL_CENTER_X    (RIGHT_PANEL_W / 2)   /* 面板内水平居中 = 200 */
#define BTN_DIAMETER        80
#define BTN_TOP_Y           110
#define BTN_MID_Y           (LCD_V_RES / 2)
#define BTN_BOT_Y           (LCD_V_RES - 110)

/* ================================================================
 *  LED 亮度与按钮动画
 * ================================================================ */
#define LED_BRIGHTNESS      204           /* ~80% */
#define ZOOM_NORMAL         256           /* LV_IMG_ZOOM_NONE */
#define ZOOM_PRESSED        230           /* 90% */

/* ================================================================
 *  ADC 按键扫描参数
 * ================================================================ */
#define KEY_SCAN_PERIOD_MS      50
#define KEY_DEBOUNCE_SAMPLES    3
#define KEY_LONG_PRESS_MS       1000

/* ================================================================
 *  电影遮幅高度
 * ================================================================ */
#define LETTERBOX_BAR_H     30

/* ================================================================
 *  嵌入 GIF 数据的链接器符号声明
 *  CMakeLists.txt 中 target_add_binary_data() 自动生成这些符号
 * ================================================================ */
extern const uint8_t _binary_dance_1_gif_start[];
extern const uint8_t _binary_dance_1_gif_end[];
extern const uint8_t _binary_dance_2_gif_start[];
extern const uint8_t _binary_dance_2_gif_end[];
extern const uint8_t _binary_dance_3_gif_start[];
extern const uint8_t _binary_dance_3_gif_end[];

/* ================================================================
 *  右侧面板布局（横排按钮）
 * ================================================================ */
#define RP_BTN_DIAM        88
#define RP_ARROW_DIAM      56
#define RP_TITLE_Y         24
#define RP_STATUS_Y        62
#define RP_GIF_NAME_Y      152
#define RP_GIF_IDX_Y       190
#define RP_RGB_Y           290

#ifdef __cplusplus
}
#endif
