/* VIEWE ROUND adaptation; pin map from the user's Muse firmware. */
#include "board.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "viewe_touch.h"
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_touch;
static uint8_t s_address;
int board_lcd_clock_pin(void) { return 48; }
int board_lcd_reset_pin(void) { return 11; }
esp_err_t board_init(i2c_master_bus_handle_t *bus)
{
    const gpio_config_t output = {.pin_bit_mask=(1ULL<<10)|(1ULL<<7),.mode=GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&output),"viewe","outputs");
    gpio_set_level(7,0); /* Speaker amplifier stays off. */
    gpio_set_level(10,0); vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(10,1); vTaskDelay(pdMS_TO_TICKS(200));
    const gpio_config_t input={.pin_bit_mask=1ULL,.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE};
    ESP_RETURN_ON_ERROR(gpio_config(&input),"viewe","BOOT");
    const i2c_master_bus_config_t cfg={.i2c_port=I2C_NUM_0,.sda_io_num=17,.scl_io_num=18,
        .clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=true};
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg,&s_bus),"viewe","I2C");
    *bus=s_bus;
    const uint8_t addresses[]={0x5a,0x15};
    for(unsigned i=0;i<sizeof(addresses);i++) if(i2c_master_probe(s_bus,addresses[i],30)==ESP_OK) {
        i2c_device_config_t dev={.device_address=addresses[i],.dev_addr_length=I2C_ADDR_BIT_LEN_7,.scl_speed_hz=400000};
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus,&dev,&s_touch),"viewe","touch");
        s_address=addresses[i];
        const uint8_t wake92[]={0xd1,1},wake81[]={0xfe,0xff};
        i2c_master_transmit(s_touch,s_address==0x5a?wake92:wake81,2,30);
        break;
    }
    ESP_LOGI("viewe","VIEWE ROUND ready; touch address 0x%02x",s_address);
    return ESP_OK;
}
bool board_touch_point(int *x,int *y)
{
    if(!s_touch) return false;
    uint8_t data[7]={0}; esp_err_t e;
    if(s_address==0x5a) {
        const uint8_t reg[]={0xd0,0},ack[]={0xd0,0,0xab};
        e=i2c_master_transmit_receive(s_touch,reg,2,data,7,30);
        i2c_master_transmit(s_touch,ack,3,30);
    } else {
        uint8_t reg=2; e=i2c_master_transmit_receive(s_touch,&reg,1,data,5,30);
    }
    if(e!=ESP_OK || !viewe_touch_decode(s_address,data,x,y)) return false;
    *x=465-*x; *y=465-*y; /* Match panel rotation. */
    return true;
}
static bool s_reset,s_toggle;
static void poll_boot(void)
{
    static bool held,raw_last,stable;
    static int64_t changed,down;
    bool raw=gpio_get_level(0)==0; int64_t now=esp_timer_get_time();
    if(raw!=raw_last){raw_last=raw;changed=now;}
    if(raw!=stable && now-changed>=30000) {
        stable=raw;
        if(stable){down=now;held=false;}
        else if(!held) s_toggle=true;
    }
    if(stable && !held && now-down>=800000){held=true;s_reset=true;}
}
bool board_take_reset(void){poll_boot();bool v=s_reset;s_reset=false;return v;}
bool board_take_wave_toggle(void){poll_boot();bool v=s_toggle;s_toggle=false;return v;}
