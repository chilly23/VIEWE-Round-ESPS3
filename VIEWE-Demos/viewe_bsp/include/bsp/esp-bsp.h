#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#define BSP_LCD_H_RES 466
#define BSP_LCD_V_RES 466
#ifdef __cplusplus
extern "C" {
#endif
lv_display_t *bsp_display_start(void);
esp_err_t bsp_display_lock(int timeout_ms);
void bsp_display_unlock(void);
esp_err_t bsp_display_backlight_on(void);
esp_err_t bsp_display_backlight_off(void);
esp_err_t bsp_display_brightness_set(int percent);
i2c_master_bus_handle_t bsp_i2c_get_handle(void);
bool bsp_touch_point(int *x,int *y);
#ifdef __cplusplus
}
#endif
