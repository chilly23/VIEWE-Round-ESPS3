/* VIEWE MSM261D4030H1/INMP441-compatible I2S microphone capture.
 * GPIO and Philips timing match the existing Muse VIEWE board port. */
#include "bsp_board_extra.h"
#include "esp_check.h"
#include "esp_log.h"
static i2s_chan_handle_t s_rx;
esp_err_t bsp_extra_codec_init(void)
{
 if(s_rx)return ESP_OK;
 i2s_chan_config_t chan=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
 ESP_RETURN_ON_ERROR(i2s_new_channel(&chan,NULL,&s_rx),"mic","channel");
 i2s_std_config_t cfg={
  .clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(16000),
  .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO),
  .gpio_cfg={.mclk=I2S_GPIO_UNUSED,.bclk=GPIO_NUM_1,.ws=GPIO_NUM_42,.dout=I2S_GPIO_UNUSED,.din=GPIO_NUM_2}};
 cfg.slot_cfg.slot_bit_width=I2S_SLOT_BIT_WIDTH_32BIT;
 cfg.slot_cfg.ws_width=32;
 cfg.slot_cfg.slot_mask=I2S_STD_SLOT_BOTH;
 ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx,&cfg),"mic","I2S");
 ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx),"mic","enable");
 gpio_pulldown_en(GPIO_NUM_2);
 ESP_LOGI("mic","VIEWE microphone enabled: BCLK=1 WS=42 DATA=2, stereo 32-clock slots");return ESP_OK;
}
esp_err_t bsp_extra_i2s_read(void *buffer,size_t len,size_t *read,uint32_t timeout_ms)
{
 if(!s_rx||!buffer||!read||len%4)return ESP_ERR_INVALID_ARG;
 size_t got=0;
 esp_err_t e=i2s_channel_read(s_rx,buffer,len,&got,timeout_ms);
 *read=got;
 if(e!=ESP_OK)return e;
 int16_t *out=buffer;
 static int32_t dc[2];
 for(size_t i=0;i<got/2;i++){
  int ch=i&1;
  dc[ch]+=((int32_t)out[i]*256-dc[ch])/256;
  int32_t v=((int32_t)out[i]-dc[ch]/256)*4;
  if(v>32767) v=32767;
  if(v< -32768) v=-32768;
  out[i]=(int16_t)v;
 }
 return ESP_OK;
}
