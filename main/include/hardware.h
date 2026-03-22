#pragma once

#include "esp_err.h"

// Initialize I2C, SHT3x, PCF8574, and LCD
esp_err_t hw_init(void);

// Hardware specific tasks
void taskReadSHT(void *pvParameters);
void taskUpdateLCD(void *pvParameters);
void taskReadAnalog(void *pvParameters);
