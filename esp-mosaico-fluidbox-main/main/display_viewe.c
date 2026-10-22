/* VIEWE CO5300 adaptation: PSRAM frame slots, internal DMA bounce buffer.
 * No ESP32-S31-only direct PSRAM DMA, TE wire, or GPIO60 power switch. */
#include "display.h"
#include "config.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#define CHUNK_ROWS 8
#define BAND_PIXELS (LCD_H_RES*BAND_ROWS)
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_done,s_bus_lock;
static uint16_t *s_dma;
typedef struct {uint16_t *pixels;int left[BAND_COUNT],right[BAND_COUNT];SemaphoreHandle_t available;} frame_t;
static frame_t s_frames[2];
static unsigned s_staging;
static QueueHandle_t s_ready;
static display_stats_t s_stats;
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static const co5300_lcd_init_cmd_t s_init[]={
 {0xfe,(uint8_t[]){0},1,0},{0xc4,(uint8_t[]){0x80},1,0},
 {0x3a,(uint8_t[]){0x55},1,0},{0x35,(uint8_t[]){0},1,10},
 {0x53,(uint8_t[]){0x20},1,10},{0x51,(uint8_t[]){0x80},1,10},
 {0x63,(uint8_t[]){0xff},1,10},{0x11,NULL,0,120},{0x29,NULL,0,20}};
static bool IRAM_ATTR sent(esp_lcd_panel_io_handle_t io,esp_lcd_panel_io_event_data_t *ev,void *ctx)
{BaseType_t wake=pdFALSE;xSemaphoreGiveFromISR(s_done,&wake);return wake==pdTRUE;}
static void sender(void *arg)
{
 unsigned slot;
 for(;;){
  xQueueReceive(s_ready,&slot,portMAX_DELAY);frame_t *f=&s_frames[slot];
  int64_t start=esp_timer_get_time();
  xSemaphoreTake(s_bus_lock,portMAX_DELAY);
  for(int b=0;b<BAND_COUNT;b++){
   int left=f->left[b],right=f->right[b],width=right-left;if(width<=0)continue;
   for(int r=0;r<BAND_ROWS;r+=CHUNK_ROWS){
    int rows=BAND_ROWS-r<CHUNK_ROWS?BAND_ROWS-r:CHUNK_ROWS;
    memcpy(s_dma,f->pixels+b*BAND_PIXELS+r*width,rows*width*2);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel,left,b*BAND_ROWS+r,right,b*BAND_ROWS+r+rows,s_dma));
    if(xSemaphoreTake(s_done,pdMS_TO_TICKS(1000))!=pdTRUE) abort();
   }
  }
  xSemaphoreGive(s_bus_lock);
  taskENTER_CRITICAL(&s_lock);s_stats.frames++;s_stats.transfer_us+=esp_timer_get_time()-start;taskEXIT_CRITICAL(&s_lock);
  xSemaphoreGive(f->available);
 }
}
esp_err_t display_init(void)
{
 s_done=xSemaphoreCreateBinary();s_bus_lock=xSemaphoreCreateMutex();s_ready=xQueueCreate(1,sizeof(unsigned));
 s_dma=heap_caps_malloc(LCD_H_RES*CHUNK_ROWS*2,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
 ESP_RETURN_ON_FALSE(s_done&&s_bus_lock&&s_ready&&s_dma,ESP_ERR_NO_MEM,"display","DMA buffers");
 for(int i=0;i<2;i++){
  s_frames[i].pixels=heap_caps_calloc(LCD_H_RES*LCD_V_RES,2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  s_frames[i].available=xSemaphoreCreateBinary();
  ESP_RETURN_ON_FALSE(s_frames[i].pixels&&s_frames[i].available,ESP_ERR_NO_MEM,"display","frame slots");
 }
 xSemaphoreGive(s_frames[1].available);
 const spi_bus_config_t bus=CO5300_PANEL_BUS_QSPI_CONFIG(48,13,47,21,14,LCD_H_RES*CHUNK_ROWS*2);
 ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO),"display","SPI");
 esp_lcd_panel_io_spi_config_t io=CO5300_PANEL_IO_QSPI_CONFIG(12,sent,NULL);
 io.pclk_hz=40000000;io.trans_queue_depth=1;
 ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST,&io,&s_io),"display","panel IO");
 co5300_vendor_config_t vendor={.init_cmds=s_init,.init_cmds_size=sizeof(s_init)/sizeof(s_init[0]),.flags.use_qspi_interface=true};
 esp_lcd_panel_dev_config_t panel={.reset_gpio_num=11,.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB,.bits_per_pixel=16,.vendor_config=&vendor};
 ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(s_io,&panel,&s_panel),"display","panel");
 ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel),"display","reset");
 ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel),"display","init");
 ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel,true,true),"display","rotate 180");
 ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel,6,0),"display","offset");
 ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel,true),"display","on");
 /* Clear the complete 466x466 panel, including the two-pixel viewport margin. */
 uint16_t *clear=heap_caps_calloc(466*2,2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
 ESP_RETURN_ON_FALSE(clear,ESP_ERR_NO_MEM,"display","clear buffer");
 for(int y=0;y<466;y+=2){ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel,0,y,466,y+2,clear));xSemaphoreTake(s_done,portMAX_DELAY);}
 free(clear);
 return xTaskCreatePinnedToCore(sender,"display",4096,NULL,6,NULL,0)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
void display_stage_band(int b,int left,int right,const uint16_t *pixels)
{
 configASSERT(b>=0&&b<BAND_COUNT&&left>=0&&right<=LCD_H_RES&&left<=right);
 frame_t *f=&s_frames[s_staging];memcpy(f->pixels+b*BAND_PIXELS,pixels,(right-left)*BAND_ROWS*2);
 f->left[b]=left;f->right[b]=right;
}
void display_present(void)
{
 xQueueSend(s_ready,&s_staging,portMAX_DELAY);s_staging^=1;
 frame_t *f=&s_frames[s_staging];xSemaphoreTake(f->available,portMAX_DELAY);
 memset(f->left,0,sizeof(f->left));memset(f->right,0,sizeof(f->right));
}
display_stats_t display_take_stats(void)
{taskENTER_CRITICAL(&s_lock);display_stats_t r=s_stats;s_stats=(display_stats_t){0};taskEXIT_CRITICAL(&s_lock);return r;}
esp_err_t display_set_brightness(uint8_t level)
{xSemaphoreTake(s_bus_lock,portMAX_DELAY);esp_err_t e=esp_lcd_panel_io_tx_param(s_io,(0x02<<24)|(0x51<<8),&level,1);xSemaphoreGive(s_bus_lock);return e;}
