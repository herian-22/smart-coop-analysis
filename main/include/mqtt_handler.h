#pragma once

#include "esp_err.h"

// Initialize MQTT Connection
void mqtt_init(void);

// Publish functions
void mqtt_publish_sensor_data(void);
