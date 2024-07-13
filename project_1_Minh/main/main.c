#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "sht30_espidf.h"
#include "bh1750_espidf.h"
#include "publish_data.h"
#include "connect_wifi.h"
#include "connect_mqtt.h"

#define MAIN_TAG "MAIN"

void sensor_read_task(void *pvParameters)
{
    float temp_data, hum_data, lux;
    esp_err_t ret;

    while (1) 
    {
        // Read SHT30
        ret = i2c_master_sensor_sht30_read(I2C_MASTER_NUM, &temp_data, &hum_data);
        if (ret == ESP_OK) 
        {
            ESP_LOGI(MAIN_TAG, "Temperature: %.2f°C, Humidity: %.2f%%", temp_data, hum_data);
        } 
        else 
        {
            ESP_LOGE(MAIN_TAG, "Failed to read SHT30: %s", esp_err_to_name(ret));
            temp_data = hum_data = -1; // Invalid data
        }

        // Read BH1750
        lux = BH1750_ReadLightIntensity();
        if (lux >= 0) 
        {
            ESP_LOGI(MAIN_TAG, "Light: %.2f [Lux]", lux);
        } 
        else 
        {
            ESP_LOGE(MAIN_TAG, "Failed to read BH1750");
            lux = -1; // Invalid data
        }

        // Publish sensor data
        publish_sensor_data(temp_data, hum_data, lux);

        ESP_LOGI(MAIN_TAG, "--------------------");

        // Wait for 3 seconds before reading again
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void app_main(void)
{
    esp_err_t ret;

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) 
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Check if NVS was initialized successfully
    if (ret != ESP_OK) {
        ESP_LOGE(MAIN_TAG, "Failed to initialize NVS. Error: %s", esp_err_to_name(ret));
        return;
    }

    // Initialize I2C
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(MAIN_TAG, "I2C initialized successfully");

    // Initialize SHT30
    ESP_ERROR_CHECK(i2c_master_sensor_sht30_init(I2C_MASTER_NUM));
    ESP_LOGI(MAIN_TAG, "SHT30 initialized successfully");

    // Initialize BH1750
    BH1750_Init();
    ESP_LOGI(MAIN_TAG, "BH1750 initialized successfully");

    // Initialize Wi-Fi and MQTT
    wifi_init_sta();
    init_start_mqtt();

    // Wait for Wi-Fi and MQTT connection
    int retry = 0;
    while (!is_mqtt_connected() && retry < 20) 
    {
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP_LOGI(MAIN_TAG, "Waiting for MQTT connection...");
        retry++;
    }

    if (!is_mqtt_connected()) 
    {
        ESP_LOGE(MAIN_TAG, "Failed to connect to MQTT broker");
        return;
    }

    ESP_LOGI(MAIN_TAG, "MQTT connected successfully");
    vTaskDelay(pdMS_TO_TICKS(1000)); // waiting more a second

    // check MQTT client
    esp_mqtt_client_handle_t client = get_mqtt_client();
    if (client == NULL) 
    {
        ESP_LOGE(MAIN_TAG, "MQTT client is NULL");
        return;
    }

    // Create task to read sensors
    xTaskCreate(sensor_read_task, "sensor_read_task", 4096, NULL, 10, NULL);
}