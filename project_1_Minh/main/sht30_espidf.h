#ifndef _SHT30_H_
#define _SHT30_H_
#include <stdio.h>
#include "esp_log.h"
#include "sdkconfig.h"
#include "driver/i2c.h"
#include "i2c_config.h"

#define DELAY_TIME_BETWEEN_ITEMS_MS 1000

#define I2C_MASTER_SCL_IO GPIO_NUM_22               /*!< gpio number for I2C master clock */
#define I2C_MASTER_SDA_IO GPIO_NUM_21               /*!< gpio number for I2C master data  */

#define SHT30_SENSOR_ADDR 0x44
#define WRITE_BIT I2C_MASTER_WRITE              /*!< I2C master write */
#define READ_BIT I2C_MASTER_READ                /*!< I2C master read */
#define ACK_CHECK_EN 0x1                        /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS 0x0                       /*!< I2C master will not check ack from slave */
#define ACK_VAL 0x0                             /*!< I2C ack value */
#define NACK_VAL 0x1                            /*!< I2C nack value */

esp_err_t i2c_master_init(void);
esp_err_t i2c_master_sensor_sht30_init(i2c_port_t i2c_num);
esp_err_t i2c_master_sensor_sht30_read(i2c_port_t i2c_num, float *temp_data, float *hum_data);
void i2c_sht30_task(void *arg);

#endif