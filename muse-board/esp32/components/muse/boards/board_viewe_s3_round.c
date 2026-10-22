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
#include "muse_audio.h"
#include "muse_board.h"
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
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static i2s_chan_handle_t s_tx, s_rx;
static bool s_tx_on, s_rx_on;
static muse_gpio_button_t s_boot;
static atomic_int s_mic_gain_q8 = 256;
static int32_t s_mic_dc[2];

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
    return muse_gpio_button_init(&s_boot, BOOT_GPIO);
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
#if CONFIG_MUSE_VIEWE_TOUCH_MIRROR_X
    x = LCD_RES - 1 - x;
#endif
#if CONFIG_MUSE_VIEWE_TOUCH_MIRROR_Y
    y = LCD_RES - 1 - y;
#endif
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
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
        esp_lcd_panel_set_gap(s_panel, CONFIG_MUSE_VIEWE_LCD_X_OFFSET, 0) != ESP_OK ||
        esp_lcd_panel_disp_on_off(s_panel, true) != ESP_OK) return NULL;

    esp_lv_adapter_config_t adapter = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter.task_core_id = MUSE_UI_CORE;
    adapter.task_priority = MUSE_UI_PRIORITY;
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

static bool display_lock(int ms) { return esp_lv_adapter_lock(ms) == ESP_OK; }
static void send_brightness(void *p)
{
    esp_lcd_panel_io_tx_param(s_io, (0x02 << 24) | (0x51 << 8), p, 1);
}
static void set_brightness(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    uint8_t level = pct * 255 / 100;
    muse_lcd_bands_run(send_brightness, &level);
}
static void send_sleep(void *p)
{
    esp_lcd_panel_io_tx_param(s_io, (0x02 << 24) | ((*(bool *)p ? 0x10 : 0x11) << 8), NULL, 0);
}
static void panel_sleep(bool sleep)
{
    muse_lcd_bands_run(send_sleep, &sleep);
    vTaskDelay(pdMS_TO_TICKS(120));
}

/* No control-bus codec: use esp_codec_dev's software speaker volume. */
static int mic_enable(const audio_codec_data_if_t *h, esp_codec_dev_type_t type, bool on)
{
    (void)h; (void)type;
    if (on == s_rx_on) return ESP_CODEC_DEV_OK;
    esp_err_t err = on ? i2s_channel_enable(s_rx) : i2s_channel_disable(s_rx);
    if (err != ESP_OK) return ESP_CODEC_DEV_DRV_ERR;
    s_rx_on = on;
    return ESP_CODEC_DEV_OK;
}
static int spk_enable(const audio_codec_data_if_t *h, esp_codec_dev_type_t type, bool on)
{
    (void)h; (void)type;
    if (on == s_tx_on) return ESP_CODEC_DEV_OK;
    if (!on) gpio_set_level(SPK_EN, 0);
    esp_err_t err = on ? i2s_channel_enable(s_tx) : i2s_channel_disable(s_tx);
    if (err != ESP_OK) return ESP_CODEC_DEV_DRV_ERR;
    s_tx_on = on;
    if (on) gpio_set_level(SPK_EN, 1);
    return ESP_CODEC_DEV_OK;
}
static int spk_write(const audio_codec_data_if_t *h, uint8_t *data, int size)
{
    (void)h;
    size_t wrote = 0;
    return i2s_channel_write(s_tx, data, size, &wrote, 1000) == ESP_OK && wrote == (size_t)size
        ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}
static int mic_read(const audio_codec_data_if_t *h, uint8_t *data, int size)
{
    (void)h;
    size_t got = 0;
    if (i2s_channel_read(s_rx, data, size, &got, 1000) != ESP_OK || got != (size_t)size)
        return ESP_CODEC_DEV_READ_FAIL;
    int16_t *samples = (int16_t *)data;
    const int gain = atomic_load_explicit(&s_mic_gain_q8, memory_order_relaxed);
    for (int i = 0; i < size / 2; i++) {
        int32_t *dc = &s_mic_dc[i & 1];
        *dc += (samples[i] * 256 - *dc) >> 8;
        int64_t v = (int64_t)(samples[i] - (*dc >> 8)) * gain / 256;
        samples[i] = v > INT16_MAX ? INT16_MAX : v < INT16_MIN ? INT16_MIN : (int16_t)v;
    }
    return ESP_CODEC_DEV_OK;
}
static void set_mic_gain(esp_codec_dev_handle_t mic, int db)
{
    (void)mic;
    if (db < 0) db = 0;
    if (db > 42) db = 42;
    atomic_store_explicit(&s_mic_gain_q8, (int)(256.0f * powf(10.0f, db / 20.0f)), memory_order_relaxed);
}
static esp_err_t audio_init(esp_codec_dev_handle_t *spk, esp_codec_dev_handle_t *mic)
{
    i2s_chan_config_t rx_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_chan_config_t tx_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    tx_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&rx_cfg, NULL, &s_rx), TAG, "mic channel");
    ESP_RETURN_ON_ERROR(i2s_new_channel(&tx_cfg, &s_tx, NULL), TAG, "speaker channel");
    i2s_std_config_t rx = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MUSE_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = MIC_CLK, .ws = MIC_WS,
                     .din = MIC_DATA, .dout = I2S_GPIO_UNUSED},
    };
    /* INMP441 needs 64 BCLK clocks per frame. Capture the high 16 bits of
     * its 24-bit sample, retaining separate left/right slots for Muse. */
    rx.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    rx.slot_cfg.ws_width = 32;
    const i2s_std_config_t tx = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MUSE_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = SPK_CLK, .ws = SPK_WS,
                     .dout = SPK_DATA, .din = I2S_GPIO_UNUSED},
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &rx), TAG, "mic bus");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &tx), TAG, "speaker bus");
    gpio_pulldown_en(MIC_DATA);
    static const audio_codec_data_if_t spk_if = {.enable = spk_enable, .write = spk_write};
    static const audio_codec_data_if_t mic_if = {.enable = mic_enable, .read = mic_read};
    esp_codec_dev_cfg_t out = {.dev_type = ESP_CODEC_DEV_TYPE_OUT, .data_if = &spk_if};
    esp_codec_dev_cfg_t in = {.dev_type = ESP_CODEC_DEV_TYPE_IN, .data_if = &mic_if};
    *spk = esp_codec_dev_new(&out);
    *mic = esp_codec_dev_new(&in);
    return *spk && *mic ? ESP_OK : ESP_ERR_NO_MEM;
}
static unsigned poll_buttons(void) { return muse_gpio_button_poll(&s_boot); }
static esp_err_t power_off(void)
{
    set_brightness(0);
    panel_sleep(true);
    gpio_set_level(SPK_EN, 0);
    while (gpio_get_level(BOOT_GPIO) == 0) vTaskDelay(pdMS_TO_TICKS(20));
    esp_sleep_enable_ext0_wakeup(BOOT_GPIO, 0);
    esp_deep_sleep_start();
    return ESP_FAIL;
}
static const muse_board_t s_board = {
    .name = "VIEWE-ROUND-V1.1",
    .width = LCD_RES, .height = LCD_RES, .round = true, .touch = true, .touch_talk = true,
    .diagonal_in = 1.5f,
    .talk_button = "boot", .talk_hint = {LV_ALIGN_BOTTOM_MID, 0, -28},
    .frame_ms = 40,
    .init = init, .display_start = display_start,
    .display_lock = display_lock, .display_unlock = esp_lv_adapter_unlock,
    .set_brightness = set_brightness, .panel_sleep = panel_sleep,
    .audio_init = audio_init, .mic_slot = CONFIG_MUSE_VIEWE_MIC_SLOT,
    .set_mic_gain = set_mic_gain, .poll_buttons = poll_buttons, .power_off = power_off,
};
const muse_board_t *muse_board_get(void) { return &s_board; }
