/* VIEWE bridge for the example's QMI8658 API subset. */
#pragma once
#include "bsp/esp-bsp.h"
#define QMI8658_ADDRESS_HIGH 0x6b
#define QMI8658_ACCEL_RANGE_8G 2
#define QMI8658_ACCEL_ODR_500HZ 4
#define QMI8658_CTRL5 6
typedef struct {i2c_master_dev_handle_t handle;bool touch_fallback;} qmi8658_dev_t;
typedef struct {float accelX,accelY,accelZ;} qmi8658_data_t;
esp_err_t qmi8658_init(qmi8658_dev_t *dev,i2c_master_bus_handle_t bus,int address);
esp_err_t qmi8658_read_sensor_data(qmi8658_dev_t *dev,qmi8658_data_t *data);
esp_err_t qmi8658_is_data_ready(qmi8658_dev_t *dev,bool *ready);
esp_err_t qmi8658_set_accel_range(qmi8658_dev_t *dev,int value);
esp_err_t qmi8658_set_accel_odr(qmi8658_dev_t *dev,int value);
esp_err_t qmi8658_set_accel_unit_mps2(qmi8658_dev_t *dev,bool value);
esp_err_t qmi8658_write_register(qmi8658_dev_t *dev,uint8_t reg,uint8_t value);
