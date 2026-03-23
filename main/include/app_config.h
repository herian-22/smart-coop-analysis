#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_pm.h"
#include "driver/gpio.h"

// --- 1. PIN CONFIGURATIONS ---
#define I2C_SDA_GPIO (gpio_num_t)21
#define I2C_SCL_GPIO (gpio_num_t)22

// --- 2. ENVIRONMENT & AI CONFIGURATIONS ---
// Physical boundaries for what the REAL environment calls "Dangerous"
#define ENV_TEMP_MIN 20.0f
#define ENV_TEMP_MAX 34.0f   
#define ENV_HUM_MIN 40.0f
#define ENV_HUM_MAX 90.0f

// --- 3. TINYML CONFIGURATIONS ---
#define AI_WINDOW_SIZE 30
#define AI_NUM_FEATURES 2
#define AI_INFERENCE_STRIDE 5
#define AI_WARMUP_SAMPLES 30

// --- 4. EVENT GROUP BITS ---
#define WIFI_CONNECTED_BIT  BIT0
#define MQTT_CONNECTED_BIT  BIT1

// Model training statistics (Static - used for shifting)
#define MODEL_TRAINING_MEAN_TEMP 25.1016f
#define MODEL_TRAINING_MEAN_HUM  50.0806f
#define MODEL_TRAINING_STD       0.75f  // Approximate

// --- 3. GLOBAL ENUMS & DATA STRUCTURES ---
enum SystemState { BOOTING, CONFIG_WIFI, CONNECTED, RUNNING };

struct SampleData {
    float temp;
    float hum;
};

struct SensorData {
    float temp = 0.0;
    float hum = 0.0;
    char date_str[20] = "Waiting Sync...";
    char connected_ssid[32] = "Disconnect";
    bool anomaly_detected = false;
    // PICO & AI Metrics (populated by TinyML task)
    float mae = 0.0;
    uint32_t inference_count = 0;
    uint32_t latency_us = 0;
    float cpu_load = 0.0;
    size_t memory_used = 0;
    // Training/Learning Metrics
    int recent_count = 0;
    bool is_training = false;
    float golden_temp[3];
    float golden_hum[3];
};

// --- 3. EXTERN GLOBAL VARIABLES ---
extern SystemState currentState;
extern SensorData dataKandang;
extern SemaphoreHandle_t dataMutex;
extern EventGroupHandle_t system_event_group;
extern QueueHandle_t sensorQueue;

