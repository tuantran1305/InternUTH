#ifndef WIFI_MQTT_H
#define WIFI_MQTT_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mqtt_client.h"

// define SSID and Password WiFi
#define WIFI_SSID "OPAL COFFEE"
#define WIFI_PASS "opalcoffee"

// define MQTT Broker
#define MQTT_BROKER_URI "mqtt://212.38.94.144:8081"
#define MQTT_PORT 1884
#define MQTT_USERNAME "hAtOjp2kejMeaJMFfpQE"

// Function prototypes
void wifi_init(void);
void mqtt_app_start(void);
void send_heartbeat_data(int heart_rate, int spo2);

#endif // WIFI_MQTT_H
