/*
 * GT1151 Capacitive Touch Panel Driver
 */
#pragma once
#include "esp_err.h"

/**
 * Initialize GT1151 touch controller.
 * Configures I2C (SDA=GPIO0, SCL=GPIO1) and creates LVGL input device.
 * Must be called after lvgl_port_init().
 */
esp_err_t tp_gt1151_init(void);
