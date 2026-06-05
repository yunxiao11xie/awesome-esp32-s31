/*
 * Smart Panel UI for RGB LED control
 */
#pragma once
#include "lvgl.h"

typedef enum {
    LED_STATE_OFF = 0,
    LED_STATE_RED,
    LED_STATE_GREEN,
    LED_STATE_BLUE,
} led_state_t;

/**
 * Create the smart panel UI with modern button controls.
 * Should be called after lvgl_port_init().
 * @param parent parent object (usually the screen)
 * @return the panel container object
 */
lv_obj_t *ui_smart_panel_create(lv_obj_t *parent);

/**
 * Get the current LED state.
 */
led_state_t ui_smart_panel_get_led_state(void);
