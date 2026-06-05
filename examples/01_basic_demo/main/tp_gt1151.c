/*
 * GT1151 Capacitive Touch Panel Driver
 * I2C: SDA=GPIO0, SCL=GPIO1 (from schematic + esp-mgba reference)
 * I2C Address: 0x14 (7-bit), confirmed by esp-mgba project's board_devices.yaml
 * Used on ESP32-S31-Korvo-1 V1.1 with 4.3" 800x480 ST7262E43 LCD
 */
#include "tp_gt1151.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <string.h>

static const char *TAG = "GT1151";

/* GT1151 I2C addresses (7-bit)
 * esp-mgba project (board_devices.yaml): 8-bit 0x28 = 7-bit 0x14
 * 0x14: INT pin low during reset (this board's config)
 * 0x5D: INT pin high during reset (fallback)
 */
#define GT1151_ADDR             0x14
#define GT1151_ADDR_ALT         0x5D

/* I2C GPIO pins - from schematic */
#define GT1151_I2C_SDA          GPIO_NUM_0
#define GT1151_I2C_SCL          GPIO_NUM_1

/* GT1151 Register addresses (16-bit, big-endian over I2C) */
#define GT1151_REG_CONFIG       0x8047  /* Config version */
#define GT1151_REG_PRODUCT_ID   0x8140  /* Product ID: "1151" or "1158" */
#define GT1151_REG_FIRMWARE     0x8144  /* Firmware version */
#define GT1151_REG_TOUCH_DATA   0x814E  /* Touch status + coordinates */

/* I2C handles */
static i2c_master_bus_handle_t  bus_handle  = NULL;
static i2c_master_dev_handle_t  dev_handle  = NULL;

/* Last known touch state */
static bool last_pressed = false;
static uint16_t last_x = 0;
static uint16_t last_y = 0;

/* ==================== I2C helpers ==================== */

static esp_err_t gt1151_read_regs(uint16_t reg, uint8_t *buf, size_t len)
{
    uint8_t reg_buf[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    return i2c_master_transmit_receive(dev_handle, reg_buf, 2, buf, len, 100);
}

static esp_err_t gt1151_write_regs(uint16_t reg, const uint8_t *data, size_t len)
{
    uint8_t *write_buf = malloc(len + 2);
    if (!write_buf) return ESP_ERR_NO_MEM;
    write_buf[0] = (uint8_t)(reg >> 8);
    write_buf[1] = (uint8_t)(reg & 0xFF);
    memcpy(write_buf + 2, data, len);
    esp_err_t ret = i2c_master_transmit(dev_handle, write_buf, len + 2, 100);
    free(write_buf);
    return ret;
}

/* ==================== LVGL input device callback ==================== */

static void tp_lvgl_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    /* Read 7 bytes from 0x814E: [status][track_id][Xh][Xl][Yh][Yl][size] */
    uint8_t touch_buf[7] = {0};

    esp_err_t err = gt1151_read_regs(GT1151_REG_TOUCH_DATA, touch_buf, 7);
    if (err != ESP_OK) {
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = last_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        return;
    }

    /* Byte 0: status (bit7=buffer_ready, bits3-0=touch_count) */
    uint8_t status = touch_buf[0];

    if ((status & 0x80) && (status & 0x0F) > 0) {
        /* GT1151 coordinates are big-endian */
        uint16_t x = ((uint16_t)touch_buf[2] << 8) | touch_buf[3];
        uint16_t y = ((uint16_t)touch_buf[4] << 8) | touch_buf[5];

        /* Clamp to screen bounds */
        if (x >= 800) x = 799;
        if (y >= 480) y = 479;

        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
        last_x = x; last_y = y; last_pressed = true;

        ESP_LOGI(TAG, "Touch: x=%u y=%u", x, y);

        /* Clear status byte so GT1151 sends new data */
        uint8_t clr = 0;
        gt1151_write_regs(GT1151_REG_TOUCH_DATA, &clr, 1);
    } else {
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_RELEASED;
        last_pressed = false;
    }
}

/* ==================== Initialization ==================== */

static esp_err_t gt1151_try_init(uint8_t addr)
{
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,  /* GT1151 supports 400kHz */
    };

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add device at 0x%02X", addr);
        return ret;
    }

    /* 等待 GT1151 稳定 */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 读取 Product ID 验证设备 — 正确的设备应返回 "1151" 等 */
    uint8_t pid[4] = {0};
    ret = gt1151_read_regs(GT1151_REG_PRODUCT_ID, pid, 4);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Product ID at 0x%02X: %c%c%c%c (0x%02X 0x%02X 0x%02X 0x%02X)",
                 addr,
                 pid[0] >= 0x20 ? pid[0] : '.', pid[1] >= 0x20 ? pid[1] : '.',
                 pid[2] >= 0x20 ? pid[2] : '.', pid[3] >= 0x20 ? pid[3] : '.',
                 pid[0], pid[1], pid[2], pid[3]);

        /* GT1151 Product ID 应该是 "1151" 或 "1158" */
        if (pid[0] == '1' && pid[1] == '1' && (pid[2] == '5') && (pid[3] == '1' || pid[3] == '8')) {
            ESP_LOGI(TAG, "GT1151 confirmed at 0x%02X (valid Product ID)", addr);
            return ESP_OK;
        }

        /* 如果 PID 全0, 说明 I2C 通信成功但设备未正确响应 */
        if (pid[0] == 0 && pid[1] == 0 && pid[2] == 0 && pid[3] == 0) {
            ESP_LOGW(TAG, "Product ID all zeros at 0x%02X — device not responding properly", addr);
            i2c_master_bus_rm_device(dev_handle);
            dev_handle = NULL;
            return ESP_ERR_NOT_FOUND;
        }
    }

    /* Product ID 不匹配 — 尝试读取触摸状态判断设备是否存在 */
    uint8_t touch_data[7] = {0};
    ret = gt1151_read_regs(GT1151_REG_TOUCH_DATA, touch_data, 7);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Touch data at 0x%02X: status=0x%02X", addr, touch_data[0]);
        /* 如果 status byte 有 buffer_ready 标志, 设备存在 */
        if (touch_data[0] & 0x80) {
            ESP_LOGI(TAG, "GT1151 detected at 0x%02X (status=0x%02X)", addr, touch_data[0]);
            return ESP_OK;
        }
    }

    /* 最后尝试 config version */
    uint8_t cfg_ver = 0;
    ret = gt1151_read_regs(GT1151_REG_CONFIG, &cfg_ver, 1);
    if (ret == ESP_OK && cfg_ver != 0) {
        ESP_LOGI(TAG, "GT1151 detected at 0x%02X (cfg_ver=0x%02X)", addr, cfg_ver);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "No valid GT1151 at 0x%02X", addr);
    i2c_master_bus_rm_device(dev_handle);
    dev_handle = NULL;
    return ESP_ERR_NOT_FOUND;
}

esp_err_t tp_gt1151_init(void)
{
    ESP_LOGI(TAG, "Initializing GT1151 (SDA=%d, SCL=%d)", GT1151_I2C_SDA, GT1151_I2C_SCL);

    /* ── Step 1: Create I2C master bus ── */
    i2c_master_bus_config_t bus_config = {
        .i2c_port = -1,
        .sda_io_num = GT1151_I2C_SDA,
        .scl_io_num = GT1151_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .trans_queue_depth = 4,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

    /* ── Step 2: Wait for GT1151 to stabilize ── */
    vTaskDelay(pdMS_TO_TICKS(200));

    /* ── Step 3: Try address 0x14 first (confirmed by esp-mgba project) ── */
    esp_err_t ret = gt1151_try_init(GT1151_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Not found at 0x%02X, trying 0x%02X...", GT1151_ADDR, GT1151_ADDR_ALT);
        ret = gt1151_try_init(GT1151_ADDR_ALT);
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GT1151 not found at either address");
        return ESP_ERR_NOT_FOUND;
    }

    /* ── Step 4: Create LVGL input device (v8 API) ── */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = tp_lvgl_read_cb;
    lv_indev_drv_register(&indev_drv);

    ESP_LOGI(TAG, "GT1151 touch ready");
    return ESP_OK;
}
