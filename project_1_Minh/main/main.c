#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "spi_flash_mmap.h"
#include "max30102.h"
#include "dht11.h"
#include "connect_wifi.h"
#include "connect_mqtt.h"


#define I2C_SDA 21
#define I2C_SCL 22
#define I2C_FRQ 100000
#define I2C_PORT I2C_NUM_0

max30102_config_t max30102 = {};

esp_err_t i2c_master_init(i2c_port_t i2c_port){
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_SDA;
    conf.scl_io_num = I2C_SCL;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_FRQ;
    i2c_param_config(i2c_port, &conf);
    return i2c_driver_install(i2c_port, I2C_MODE_MASTER, 0, 0, 0);
}

void get_bpm(void* param) {
    printf("MAX30102 Test\n");
    max30102_data_t result = {};
    /*ESP_ERROR_CHECK(max30102_print_registers(&max30102));*/
    while(true) {
        //Update sensor, saving to "result"
        ESP_ERROR_CHECK(max30102_update(&max30102, &result));
        if(result.pulse_detected) {
            printf("BEAT\n");
            printf("BPM: %f | SpO2: %f%%\n", result.heart_bpm, result.spO2);
        }
        //Update rate: 100Hz
        vTaskDelay(10/portTICK_PERIOD_MS);
    }
}



void app_main(void)
{
    // //Init I2C_NUM_0
    // ESP_ERROR_CHECK(i2c_master_init(I2C_PORT));
    // //Init sensor at I2C_NUM_0
    // ESP_ERROR_CHECK(max30102_init( &max30102, I2C_PORT,
    //                MAX30102_DEFAULT_OPERATING_MODE,
    //                MAX30102_DEFAULT_SAMPLING_RATE,
    //                MAX30102_DEFAULT_LED_PULSE_WIDTH,
    //                MAX30102_DEFAULT_IR_LED_CURRENT,
    //                MAX30102_DEFAULT_START_RED_LED_CURRENT,
    //                MAX30102_DEFAULT_MEAN_FILTER_SIZE,
    //                MAX30102_DEFAULT_PULSE_BPM_SAMPLE_SIZE,
    //                MAX30102_DEFAULT_ADC_RANGE, 
    //                MAX30102_DEFAULT_SAMPLE_AVERAGING,
    //                MAX30102_DEFAULT_ROLL_OVER,
    //                MAX30102_DEFAULT_ALMOST_FULL,
    //                false ));
    
    // //Start test task
    // xTaskCreate(get_bpm, "Get BPM", 8192, NULL, 1, NULL);

    //DHT11
    DHT11_init(GPIO_NUM_25);

    while(1) 
    {
        printf("Temperature is %d \n", DHT11_read().temperature);
        printf("Humidity is %d\n", DHT11_read().humidity);
        printf("Status code is %d\n", DHT11_read().status);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    //Connect wifi
        // Khởi tạo NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Khởi tạo và kết nối WiFi
    wifi_init_sta();
    init_start_mqtt();
}
