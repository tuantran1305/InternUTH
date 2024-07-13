#ifndef PUBLISH_DATA_H
#define PUBLISH_DATA_H

#include "esp_err.h"
#include "mqtt_client.h"

void publish_sensor_data(float temperature, float humidity, float light);

#endif // PUBLISH_DATA_H