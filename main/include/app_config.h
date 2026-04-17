#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"

// --- 1. PIN CONFIGURATIONS ---
#define I2C_SDA_GPIO (gpio_num_t)21
#define I2C_SCL_GPIO (gpio_num_t)22
#define BUZZER_GPIO_NUM  33

// --- 2. TEMPERATURE THRESHOLDS (°C) ---
#define TEMP_COLD_THRESHOLD     22.0f   // Below this = COLD
#define TEMP_HOT_THRESHOLD      30.0f   // Above this = HOT
#define TEMP_HYSTERESIS         0.5f    // Hysteresis band to prevent jitter

// --- 3. DISPLAY MODE (temperature-reactive) ---
typedef enum {
    DISPLAY_MODE_NORMAL,   // 22–30°C: Happy face, mouth smile
    DISPLAY_MODE_COLD,     // <22°C:   Squinting eyes, chattering mouth, shiver
    DISPLAY_MODE_HOT,      // >30°C:   Sweating, mouth open, backlight blink
    DISPLAY_MODE_ERROR     // Sensor fail: confused face (X eyes)
} DisplayMode;

#define SHT_FAIL_THRESHOLD  5   // Consecutive failures before ERROR mode

// --- 4. BUZZER COMMANDS ---
typedef enum {
    BUZZER_CMD_BOOT,           // Cheerful boot melody
    BUZZER_CMD_STATE_CHANGE,   // Dual beep on mode transition
    BUZZER_CMD_ALARM_HOT       // Rapid alarm for T>35°C
} buzzer_cmd_t;

// --- 5. SYSTEM STATE ---
enum SystemState { BOOTING, FACE_ANIM };

// --- 6. SENSOR DATA ---
struct SensorData {
    float temp = 0.0f;
    float hum  = 0.0f;
};

// --- 7. EXTERN GLOBAL VARIABLES ---
extern SystemState currentState;
extern DisplayMode currentDisplayMode;
extern SensorData  sensorData;
extern SemaphoreHandle_t dataMutex;
