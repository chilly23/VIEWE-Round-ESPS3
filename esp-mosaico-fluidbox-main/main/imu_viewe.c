/* QMI8658 accelerometer if positively identified, otherwise touch gravity.
 * Register meanings checked against SensorLib in the supplied Waveshare ZIP. */
#include "imu.h"
#include "board.h"
#include "config.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>
static i2c_master_dev_handle_t s_dev;
static float s_lp[3];
static bool s_primed;
static esp_err_t read_reg(uint8_t reg,void *out,size_t n)
{return i2c_master_transmit_receive(s_dev,&reg,1,out,n,30);}
static esp_err_t write_reg(uint8_t reg,uint8_t value)
{uint8_t p[]={reg,value};return i2c_master_transmit(s_dev,p,2,30);}
esp_err_t imu_init(i2c_master_bus_handle_t bus)
{
    const uint8_t addresses[]={0x6a,0x6b};
    for(unsigned i=0;i<sizeof(addresses);i++) {
        if(i2c_master_probe(bus,addresses[i],30)!=ESP_OK) continue;
        i2c_device_config_t cfg={.device_address=addresses[i],.dev_addr_length=I2C_ADDR_BIT_LEN_7,.scl_speed_hz=400000};
        if(i2c_master_bus_add_device(bus,&cfg,&s_dev)!=ESP_OK) continue;
        uint8_t id=0;
        if(read_reg(0,&id,1)==ESP_OK && id==5 && write_reg(8,0)==ESP_OK &&
           write_reg(2,0x40)==ESP_OK && write_reg(3,0x26)==ESP_OK && write_reg(8,1)==ESP_OK) {
            ESP_LOGI("imu","QMI8658 identified at 0x%02x; accelerometer tilt enabled",addresses[i]);
            return ESP_OK;
        }
        i2c_master_bus_rm_device(s_dev);s_dev=NULL;
    }
    ESP_LOGW("imu","No supported IMU identified; drag on screen to steer gravity");
    return ESP_OK;
}
bool imu_read(float dt,sim_forces_t *out)
{
    int x,y;
    if(board_touch_point(&x,&y)) {
        memset(out,0,sizeof(*out));
        float dx=(233-x)/233.0f,dy=(233-y)/233.0f;
        out->gravity[0]=dx*GRAVITY_GAIN*GRAVITY_MPS2*PX_PER_METER;
        out->gravity[1]=dy*GRAVITY_GAIN*GRAVITY_MPS2*PX_PER_METER;
        return true;
    }
    if(!s_dev) return false;
    uint8_t bytes[6];
    if(read_reg(0x35,bytes,6)!=ESP_OK) return false;
    float a[3];
    for(int i=0;i<3;i++) a[i]=(int16_t)(bytes[2*i]|bytes[2*i+1]<<8)*(8.0f*GRAVITY_MPS2/32768.0f);
    const float k=s_primed ? 1-expf(-6.2831853f*GRAVITY_LP_HZ*fminf(dt,.25f)) : 1;
    for(int i=0;i<3;i++) s_lp[i]+=k*(a[i]-s_lp[i]);
    s_primed=true;
    float mag=sqrtf(s_lp[0]*s_lp[0]+s_lp[1]*s_lp[1]+s_lp[2]*s_lp[2]);
    if(mag<.5f) return false;
    memset(out,0,sizeof(*out));
    float scale=GRAVITY_GAIN*GRAVITY_MPS2*PX_PER_METER/mag;
    out->gravity[0]=-s_lp[0]*scale;
    out->gravity[1]=s_lp[1]*scale;
    out->gravity[2]=s_lp[2]*scale;
    return true;
}
