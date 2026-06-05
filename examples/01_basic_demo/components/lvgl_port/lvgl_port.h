#pragma once
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

esp_err_t lvgl_port_init(esp_lcd_panel_handle_t panel, uint16_t *fb);
void lvgl_port_handler(void);
void lvgl_port_lock(void);
void lvgl_port_unlock(void);
