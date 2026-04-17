/**
 * @file main.cpp
 * @brief Entry point for Context-Aware LCD Face Animation + Buzzer on ESP32.
 *
 * Boot flow:
 *   1. Init NVS + mutexes
 *   2. Init buzzer (LEDC PWM)
 *   3. Init hardware (I2C, SHT31, LCD, CGRAM)
 *   4. Play boot melody
 *   5. Start tasks: LCD, SHT31, Buzzer
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "app_config.h"
#include "hardware.h"
#include "lcd_face.h"

static const char *TAG = "FaceAnim_Main";

// Global Variables
SystemState    currentState       = BOOTING;
DisplayMode    currentDisplayMode = DISPLAY_MODE_NORMAL;
SensorData     sensorData;
SemaphoreHandle_t dataMutex       = NULL;

extern "C" void app_main() {
    // 1. Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize mutexes
    dataMutex = xSemaphoreCreateMutex();

    // 3. Initialize Buzzer (LEDC PWM — independent of I2C)
    buzzer_init();

    // 4. Initialize Hardware (I2C, SHT31, LCD, Face CGRAM)
    ESP_ERROR_CHECK(hw_init());
    ESP_LOGI(TAG, "Hardware initialized.");

    // 5. Switch to face animation mode
    currentState = FACE_ANIM;

    // 6. Play boot melody
    buzzer_play(BUZZER_CMD_BOOT);

    // 7. Start tasks
    xTaskCreatePinnedToCore(taskUpdateLCD, "LCD_Face",   4096, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(taskReadSHT,   "SHT_Read",  4096, NULL, 3, NULL, 1);
    xTaskCreate(taskBuzzer,                "Buzzer",     2048, NULL, 1, NULL);

    ESP_LOGI(TAG, "Context-aware face animation + buzzer running!");
    ESP_LOGI(TAG, "  COLD: < %.0f°C  |  NORMAL: %.0f-%.0f°C  |  HOT: > %.0f°C",
             TEMP_COLD_THRESHOLD, TEMP_COLD_THRESHOLD, TEMP_HOT_THRESHOLD, TEMP_HOT_THRESHOLD);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}