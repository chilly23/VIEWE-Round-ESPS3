/* SPDX-License-Identifier: Apache-2.0 */
/*
 * VIEWE-ROUND-V1.1 / 202506, CO5300 AMOLED variants, 466x466.
 * Pin and protocol sources (vendor, not a Waveshare pin map):
 * https://github.com/VIEWESMART/ESP32-S3-Round-Dev-Board
 * examples/esp-idf/ESP32-S3-Round-Dev-Board/viewe1.5/main/main.c
 * viewe1.5/components/{inmp441,ns4168}/ and
 * viewe1.75/components/esp_lcd_touch_cst9217/esp_lcd_touch_cst9217.c
 *
 * The I2S MEMS mic and NS4168 have SEPARATE I2S clocks. The vendor calls
 * its mic driver INMP441; the V1.1 schematic labels an MSM261D4030H1CPM
 * with L/R tied to ground. PWR is a hardware power
 * circuit, not an invented GPIO button. The onboard BOOT/encoder press is
 * GPIO0. RTC, IMU, rotary encoder and battery gauge are not enabled here.
 * GPIO45 is the schematic's TFT-only backlight; AMOLED does not use it.
 * NS4168 timing follows its manufacturer datasheet section 10.2 (included
 * in the vendor repository's datasheet/peripheral/ directory): standard
 * Philips I2S, one BCLK delay. The vendor example's MSB slot macro omits
 * that delay and is deliberately not copied here.
 */
#include <math.h>
#include <stdint.h>
#include <stdatomic.h>
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "muse_mem.h"
#include "muse_lcd_bands.h"
#include "viewe_touch.h"

#define LCD_RES 466
#define LCD_CHUNK_BYTES (LCD_RES * 8 * 2)
#define LCD_CS GPIO_NUM_12
#define LCD_CLK GPIO_NUM_48
#define LCD_D0 GPIO_NUM_13
#define LCD_D1 GPIO_NUM_47
#define LCD_D2 GPIO_NUM_21
#define LCD_D3 GPIO_NUM_14
#define LCD_RST GPIO_NUM_11
#define TOUCH_SDA GPIO_NUM_17
#define TOUCH_SCL GPIO_NUM_18
#define TOUCH_RST GPIO_NUM_10
#define TOUCH_INT GPIO_NUM_9
#define MIC_CLK GPIO_NUM_1
#define MIC_DATA GPIO_NUM_2
#define MIC_WS GPIO_NUM_42
#define SPK_CLK GPIO_NUM_5
#define SPK_DATA GPIO_NUM_4
#define SPK_WS GPIO_NUM_6
#define SPK_EN GPIO_NUM_7
#define BOOT_GPIO GPIO_NUM_0

static const char *TAG = "viewe";
static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_tp;
static uint8_t s_tp_address;
static int64_t s_tp_retry;
static volatile int s_touch_x, s_touch_y;
static volatile bool s_touch_down;
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;






/* Vendor initialization, with brightness limited at startup. Geometry is
 * 466x466 per panel specification; the sample's 471-pixel LVGL width is not
 * used. esp_lcd_panel_set_gap keeps the offset on every partial update. */
static const co5300_lcd_init_cmd_t s_lcd_init[] = {
    {0xfe, (uint8_t[]){0x00}, 1, 0},
    {0xc4, (uint8_t[]){0x80}, 1, 0},
    {0x3a, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 10},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x51, (uint8_t[]){0x80}, 1, 10},
    {0x63, (uint8_t[]){0xff}, 1, 10},
    {0x11, NULL, 0, 120},
    {0x29, NULL, 0, 20},
};

static esp_err_t init(void)
{
    const gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << TOUCH_RST) | (1ULL << SPK_EN),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&outputs), TAG, "output pins");
    gpio_set_level(SPK_EN, 0);
    gpio_set_level(TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    const gpio_config_t touch_int = {
        .pin_bit_mask = 1ULL << TOUCH_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&touch_int), TAG, "touch interrupt");
    const i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = TOUCH_SDA, .scl_io_num = TOUCH_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &s_i2c), TAG, "i2c");
    return ESP_OK;
}

static bool touch_probe(void)
{
    const uint8_t addresses[] = {0x5a, 0x15};
    for (unsigned i = 0; i < sizeof(addresses); ++i) {
        if (i2c_master_probe(s_i2c, addresses[i], 20) != ESP_OK) continue;
        const i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addresses[i], .scl_speed_hz = 400000,
        };
        if (i2c_master_bus_add_device(s_i2c, &cfg, &s_tp) != ESP_OK) return false;
        s_tp_address = addresses[i];
        if (s_tp_address == 0x5a) {
            const uint8_t normal[] = {0xd1, 0x01};
            i2c_master_transmit(s_tp, normal, sizeof(normal), 30);
        } else {
            const uint8_t awake[] = {0xfe, 0xff};
            i2c_master_transmit(s_tp, awake, sizeof(awake), 30);
        }
        ESP_LOGI(TAG, "touch: %s at 0x%02x", s_tp_address == 0x5a ? "CST9217" : "CST816/820", s_tp_address);
        return true;
    }
    return false;
}

static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->state = LV_INDEV_STATE_RELEASED;
    s_touch_down = false;
    if (!s_tp) {
        int64_t now = esp_timer_get_time();
        if (now < s_tp_retry) return;
        s_tp_retry = now + 2000000;
        if (!touch_probe()) return;
    }
    uint8_t packet[7] = {0};
    esp_err_t err;
    if (s_tp_address == 0x5a) {
        const uint8_t reg[] = {0xd0, 0x00};
        const uint8_t ack[] = {0xd0, 0x00, 0xab};
        err = i2c_master_transmit_receive(s_tp, reg, sizeof(reg), packet, 7, 30);
        i2c_master_transmit(s_tp, ack, sizeof(ack), 30);
    } else {
        const uint8_t reg = 0x02;
        err = i2c_master_transmit_receive(s_tp, &reg, 1, packet, 5, 30);
    }
    if (err != ESP_OK) return;
    int x, y;
    if (!viewe_touch_decode(s_tp_address, packet, &x, &y)) return;
    x = LCD_RES - 1 - x;
    y = LCD_RES - 1 - y;
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
    s_touch_x=x;s_touch_y=y;s_touch_down=true;
}

static void round_area(lv_event_t *event)
{
    lv_area_t *a = lv_event_get_param(event);
    a->x1 &= ~1; a->y1 &= ~1;
    a->x2 |= 1; a->y2 |= 1;
}

static lv_display_t *display_start(lv_indev_t **touch)
{
    *touch = NULL;
    const spi_bus_config_t bus = CO5300_PANEL_BUS_QSPI_CONFIG(
        LCD_CLK, LCD_D0, LCD_D1, LCD_D2, LCD_D3, LCD_CHUNK_BYTES);
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return NULL;
    esp_lcd_panel_io_spi_config_t io = CO5300_PANEL_IO_QSPI_CONFIG(LCD_CS, NULL, NULL);
    io.pclk_hz = 40 * 1000 * 1000;
    if (esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &s_io) != ESP_OK) return NULL;
    co5300_vendor_config_t vendor = {
        .init_cmds = s_lcd_init,
        .init_cmds_size = sizeof(s_lcd_init) / sizeof(s_lcd_init[0]),
        .flags.use_qspi_interface = true,
    };
    const esp_lcd_panel_dev_config_t panel = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16, .vendor_config = &vendor,
    };
    if (esp_lcd_new_panel_co5300(s_io, &panel, &s_panel) != ESP_OK ||
        esp_lcd_panel_reset(s_panel) != ESP_OK ||
        esp_lcd_panel_init(s_panel) != ESP_OK ||
        esp_lcd_panel_mirror(s_panel, true, true) != ESP_OK ||
        esp_lcd_panel_set_gap(s_panel, 6, 0) != ESP_OK ||
        esp_lcd_panel_disp_on_off(s_panel, true) != ESP_OK) return NULL;

    esp_lv_adapter_config_t adapter = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter.task_core_id = MUSE_UI_CORE;
    adapter.task_priority = MUSE_UI_PRIORITY;
#if CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT > 1
    /* Brookesia's nested rendering requires more than the 8KB default. */
    adapter.task_stack_size = 32768;
#endif
    if (esp_lv_adapter_init(&adapter) != ESP_OK) return NULL;
    const esp_lv_adapter_display_config_t disp_cfg = {
        .panel = s_panel, .panel_io = s_io,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
            .rotation = ESP_LV_ADAPTER_ROTATE_0,
            .hor_res = LCD_RES, .ver_res = LCD_RES,
        },
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
    };
    lv_display_t *disp = muse_lcd_bands_register(disp_cfg, 120, LCD_CHUNK_BYTES);
    if (!disp) return NULL;
    lv_display_add_event_cb(disp, round_area, LV_EVENT_INVALIDATE_AREA, NULL);
    *touch = lv_indev_create();
    if (!*touch) return NULL;
    lv_indev_set_type(*touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(*touch, disp);
    lv_indev_set_read_cb(*touch, touch_read);
    if (!touch_probe()) ESP_LOGW(TAG, "Touch not responding yet; retrying. BOOT still confirms pairing.");
    if (esp_lv_adapter_start() != ESP_OK) return NULL;
    return disp;
}


static lv_display_t *s_display;
static void display_health(void *arg) {
 for(;;) {
  vTaskDelay(pdMS_TO_TICKS(5000));
  ESP_LOGI("viewe", "display alive: %u bands complete; internal RAM %u", (unsigned)muse_lcd_bands_completed(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
 }
}
lv_display_t *bsp_display_start(void) {
 if(s_display) return s_display;
 if(init()!=ESP_OK)return NULL;
 lv_indev_t *touch;
 s_display=display_start(&touch);
 if(s_display) {
  ESP_LOGI("viewe", "VIEWE demo display started: CO5300 466x466");
  if(xTaskCreate(display_health,"display_health",2048,NULL,1,NULL)!=pdPASS) ESP_LOGW("viewe","Health logging unavailable");
 }
 return s_display;
}
esp_err_t bsp_display_lock(int ms){return esp_lv_adapter_lock(ms);}
void bsp_display_unlock(void){esp_lv_adapter_unlock();}
i2c_master_bus_handle_t bsp_i2c_get_handle(void){return s_i2c;}
static void brightness(void *p){esp_lcd_panel_io_tx_param(s_io,(0x02<<24)|(0x51<<8),p,1);}
esp_err_t bsp_display_brightness_set(int pct){
 if(pct<0||pct>100)return ESP_ERR_INVALID_ARG;
 uint8_t value=pct*255/100;muse_lcd_bands_run(brightness,&value);return ESP_OK;
}
esp_err_t bsp_display_backlight_on(void){return bsp_display_brightness_set(50);}
esp_err_t bsp_display_backlight_off(void){return bsp_display_brightness_set(0);}
bool bsp_touch_point(int *x,int *y){*x=s_touch_x;*y=s_touch_y;return s_touch_down;}
