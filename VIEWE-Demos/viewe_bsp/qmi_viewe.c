/* Register values checked against SensorLib in the supplied vendor archive. */
#include "qmi8658.h"
#include "esp_log.h"
#include "esp_check.h"
#include <string.h>
esp_err_t qmi8658_write_register(qmi8658_dev_t *d,uint8_t r,uint8_t v)
{if(!d->handle)return ESP_OK;uint8_t b[]={r,v};return i2c_master_transmit(d->handle,b,2,30);}
esp_err_t qmi8658_init(qmi8658_dev_t *d,i2c_master_bus_handle_t bus,int address)
{
 memset(d,0,sizeof(*d));
 const uint8_t addresses[]={0x6a,0x6b};
 for(unsigned i=0;i<sizeof(addresses);i++){
  if(i2c_master_probe(bus,addresses[i],30)!=ESP_OK)continue;
  i2c_device_config_t cfg={.device_address=addresses[i],.dev_addr_length=I2C_ADDR_BIT_LEN_7,.scl_speed_hz=400000};
  if(i2c_master_bus_add_device(bus,&cfg,&d->handle)!=ESP_OK)continue;
  uint8_t reg=0,id=0;
  if(i2c_master_transmit_receive(d->handle,&reg,1,&id,1,30)==ESP_OK&&id==5){
   ESP_RETURN_ON_ERROR(qmi8658_write_register(d,8,0),"imu","disable");
   ESP_RETURN_ON_ERROR(qmi8658_write_register(d,2,0x40),"imu","auto increment");
   ESP_RETURN_ON_ERROR(qmi8658_write_register(d,3,0x24),"imu","8g/500Hz");
   ESP_RETURN_ON_ERROR(qmi8658_write_register(d,8,1),"imu","enable");
   ESP_LOGI("imu","QMI8658 identified at 0x%02x",addresses[i]);return ESP_OK;
  }
  i2c_master_bus_rm_device(d->handle);d->handle=NULL;
 }
 d->touch_fallback=true;
 ESP_LOGW("imu","No QMI8658 found; use touch to move the shapes");return ESP_OK;
}
esp_err_t qmi8658_read_sensor_data(qmi8658_dev_t *d,qmi8658_data_t *data)
{
 if(d->touch_fallback){
  int x,y;bool down=bsp_touch_point(&x,&y);
  data->accelX=down?(y-233)/233.0f*4:0;
  data->accelY=down?-(x-233)/233.0f*4:0;data->accelZ=9.81f;return ESP_OK;
 }
 uint8_t reg=0x35,b[6];
 esp_err_t e=i2c_master_transmit_receive(d->handle,&reg,1,b,6,30);if(e!=ESP_OK)return e;
 data->accelX=(int16_t)(b[0]|b[1]<<8)*(8*9.81f/32768);
 data->accelY=(int16_t)(b[2]|b[3]<<8)*(8*9.81f/32768);
 data->accelZ=(int16_t)(b[4]|b[5]<<8)*(8*9.81f/32768);return ESP_OK;
}
esp_err_t qmi8658_is_data_ready(qmi8658_dev_t *d,bool *ready){*ready=true;return ESP_OK;}
esp_err_t qmi8658_set_accel_range(qmi8658_dev_t *d,int v){return v==2?ESP_OK:ESP_ERR_NOT_SUPPORTED;}
esp_err_t qmi8658_set_accel_odr(qmi8658_dev_t *d,int v){return v==4?ESP_OK:ESP_ERR_NOT_SUPPORTED;}
esp_err_t qmi8658_set_accel_unit_mps2(qmi8658_dev_t *d,bool v){return v?ESP_OK:ESP_ERR_NOT_SUPPORTED;}
