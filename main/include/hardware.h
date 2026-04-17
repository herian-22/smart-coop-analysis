#pragma once

#include "esp_err.h"
#include <hd44780.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "app_config.h"

// Initialize I2C, SHT31, PCF8574, LCD, and Face Animation CGRAM
esp_err_t hw_init(void);

// Initialize buzzer (LEDC PWM on GPIO 33)
void buzzer_init(void);

// Send a buzzer command (non-blocking, queued)
void buzzer_play(buzzer_cmd_t cmd);

// Tasks
void taskUpdateLCD(void *pvParameters);
void taskReadSHT(void *pvParameters);
void taskBuzzer(void *pvParameters);

// LCD & I2C accessors
hd44780_t* hw_get_lcd(void);
SemaphoreHandle_t hw_get_i2c_mutex(void);
