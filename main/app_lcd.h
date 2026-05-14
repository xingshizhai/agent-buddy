#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "lvgl.h"

// Initialize RGB LCD panel, GT1151 touch, LVGL port.
// i2c_bus must already be initialized (shared with audio).
esp_err_t app_lcd_init(i2c_master_bus_handle_t i2c_bus);

lv_display_t *app_lcd_get_display(void);
lv_indev_t   *app_lcd_get_touch_indev(void);
