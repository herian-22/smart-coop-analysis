#include "tinyml_task.h"
#include "app_config.h"
#include "mqtt_handler.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include <math.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "synthetic_inference.h"
#include "model_params.h"

#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "rl_feedback.h"

// No longer using hardcoded golden samples from model_data.h

static const char* TAG = "TinyML";

// Window buffer for sensor data
static float window_buffer[AI_WINDOW_SIZE][AI_NUM_FEATURES];
static int window_idx = 0;
static bool window_full = false;

// Benchmarking structure moved up
typedef struct {
    int64_t last_latency_us;
    uint32_t inference_count;
    size_t arena_used;
    float cpu_load;
} PICOBenchmark;

static PICOBenchmark pico = {};

// --- ONLINE LEARNING / SELF-CALIBRATION ---
typedef struct {
    float temp_min;
    float temp_max;
    float hum_min;
    float hum_max;
    
    // Core statistics
    uint32_t count;
    float mean_mae;
    float M2_mae; 

    // [NEW] Runtime mean for Dynamic Shifting
    float runtime_mean_temp;
    float runtime_mean_hum;
} OnlineLearningStats;

static OnlineLearningStats stats = {
    .temp_min = ENV_TEMP_MIN, 
    .temp_max = ENV_TEMP_MAX,
    .hum_min = ENV_HUM_MIN,
    .hum_max = ENV_HUM_MAX,
    .count = 0,
    .mean_mae = 0.05f, 
    .M2_mae = 0.01f,
    .runtime_mean_temp = 0.0f,
    .runtime_mean_hum = 0.0f
};

// AI_WARMUP_SAMPLES defined in app_config.h

// Rate-of-change (trend) tracking
static float prev_mae = -1.0f;   // previous MAE value (-1 means uninitialized)
static int   rising_count = 0;   // how many consecutive samples MAE has been rising
// AI_INFERENCE_STRIDE etc. defined in app_config.h
#define TREND_ANOMALY_STEPS 4    
#define TREND_MIN_MAE       0.15f 

static int inference_stride_counter = 0; 
float normalize(float val, float min, float max) {
    if (max <= min) return 0.5f;
    float n = (val - min) / (max - min);
    ESP_LOGD(TAG, "Normalize: %.2f in [%.2f, %.2f] -> %.4f", val, min, max, n);
    return n;
}
// --- MODULAR HELPERS ---

void update_welford_stats(float mae, float raw_temp, float raw_hum) {
    if (stats.count < AI_WARMUP_SAMPLES) { 
        stats.count++;
        
        // Update MAE stats
        float delta = mae - stats.mean_mae;
        stats.mean_mae += delta / stats.count;
        float delta2 = mae - stats.mean_mae;
        stats.M2_mae += delta * delta2;

        // Update Physical Mean for Shifting
        stats.runtime_mean_temp += (raw_temp - stats.runtime_mean_temp) / stats.count;
        stats.runtime_mean_hum  += (raw_hum - stats.runtime_mean_hum) / stats.count;
    }
}

float calculate_dynamic_threshold() {
    if (stats.count < AI_WARMUP_SAMPLES) {
        return 1.0f; // Return high threshold during warmup
    }
    
    // Calculate Variance & Std Dev
    float variance = stats.M2_mae / stats.count;
    float std_dev = sqrtf(variance);
    
    // Dynamic Threshold Formula using RL multiplier
    float threshold = stats.mean_mae + (current_policy_threshold_multiplier * std_dev);
    
    ESP_LOGI(TAG, "Threshold Calc: MeanMAE=%.4f, StdDev=%.4f, RL_Mult=%.2f -> Thr=%.4f", 
             stats.mean_mae, std_dev, current_policy_threshold_multiplier, threshold);

    // Safety Clamps
    float max_allowed = 5.0f; // reasonable max absolute limit 
    if (threshold > max_allowed) {
        threshold = max_allowed;
        ESP_LOGW(TAG, "Threshold clamped to Max: %.2f", max_allowed);
    }
    if (threshold < 0.05f) {
        threshold = 0.05f;
        ESP_LOGW(TAG, "Threshold clamped to Min: 0.05");
    }
    
    return threshold;
}

bool detect_trend_anomaly(float mae) {
    if (stats.count < AI_WARMUP_SAMPLES || prev_mae < 0.0f) return false;
    
    // Ignore tiny fluctuations if error is very low
    if (mae < TREND_MIN_MAE) {
        rising_count = 0;
        return false;
    }
    
    if (mae > prev_mae) {
        rising_count++;
    } else {
        rising_count = 0; 
    }
    
    if (rising_count >= TREND_ANOMALY_STEPS) {
        ESP_LOGW(TAG, "[TREND] MAE rising %.4f -> %.4f (%d steps).", prev_mae, mae, rising_count);
        return true;
    }
    return false;
}

// --- INCREMENTAL LEARNING (EXPERIENCE REPLAY) ---
// Note: Training routines (sync_weights_to_buffer, train_last_layer) 
// are temporarily disabled as they aren't compatible with the new Conv1D model structure.
void sync_weights_to_buffer() {
    // Disabled
}

void train_last_layer(float temp, float hum) {
    // Disabled
}

// Forward declare save_tinyml_state_to_nvs so reset_autoencoder can use it
void save_tinyml_state_to_nvs();

void reset_autoencoder() {
    ESP_LOGI(TAG, "Resetting anomaly baseline...");
    stats.count = 0;
    stats.mean_mae = 0.05f;
    stats.M2_mae = 0.01f;
    save_tinyml_state_to_nvs(); // Ensure new baseline is saved
}

// --- NVS Persistent Storage Logic ---
void save_tinyml_state_to_nvs() {
    // Sync bobot ke buffer
    sync_weights_to_buffer();

    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("tinyml", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle");
        return;
    }

    // Model saving disabled for new structure
    /*
    err = nvs_set_blob(my_handle, "model_blob", mutable_model, sizeof(mutable_model));
    if (err != ESP_OK) ESP_LOGE(TAG, "Error saving model blob");
    */

    err = nvs_set_blob(my_handle, "stats_blob", &stats, sizeof(OnlineLearningStats));
    if (err != ESP_OK) ESP_LOGE(TAG, "Error saving stats blob");

    err = nvs_commit(my_handle);
    if (err != ESP_OK) ESP_LOGE(TAG, "Error committing NVS");

    nvs_close(my_handle);
    ESP_LOGI(TAG, "TinyML state saved to NVS. Samples: %lu", stats.count);
}

void load_tinyml_state_from_nvs() {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("tinyml", NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No NVS state found (First boot or erased). Using default stats.");
        return;
    }

    ESP_LOGI(TAG, "Found existing NVS state. Loading Stats...");
    // Model loading disabled for new structure
    /*
    size_t required_size = sizeof(mutable_model);
    err = nvs_get_blob(my_handle, "model_blob", mutable_model, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load model blob. Using default.");
    }
    */

    size_t required_size = sizeof(OnlineLearningStats);
    err = nvs_get_blob(my_handle, "stats_blob", &stats, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load stats blob. Using default.");
        // stats struct is already seeded with defaults
    } else {
        ESP_LOGI(TAG, "Resumed Learning State (Samples: %lu, MAE: %.4f)", stats.count, stats.mean_mae);
    }

    nvs_close(my_handle);
}

bool setup_tinyml() {
    if (!init_autoencoder()) {
        ESP_LOGE(TAG, "Failed to initialize autoencoder model.");
        return false;
    }
    load_tinyml_state_from_nvs();

    printf("\n\033[1;35m╔══════════════════════════════════════════════════════╗\033[0m\n");
    printf("\033[1;35m║          SmartCoop AI - TinyML Autoencoder           ║\033[0m\n");
    printf("\033[1;35m╠══════════════════════════════════════════════════════╣\033[0m\n");
    printf("\033[1;35m║  Model    : Windowed Sliding Autoencoder             ║\033[0m\n");
    printf("\033[1;35m║  Window   : %d samples x %d features                  ║\033[0m\n", AI_WINDOW_SIZE, AI_NUM_FEATURES);
    printf("\033[1;35m║  Stride   : setiap %d sampel baru                    ║\033[0m\n", AI_INFERENCE_STRIDE);
    printf("\033[1;35m║  Warmup   : %d sampel (threshold adapt.)              ║\033[0m\n", AI_WARMUP_SAMPLES);
    printf("\033[1;35m║  Mean T   : %.4f°C  Mean H: %.2f%%                ║\033[0m\n", MODEL_TRAINING_MEAN_TEMP, MODEL_TRAINING_MEAN_HUM);
    printf("\033[1;35m║  Samples  : %lu (dari NVS)                           ║\033[0m\n", stats.count);
    printf("\033[1;35m║  MAE avg  : %.4f (threshold adaptif)             ║\033[0m\n", stats.mean_mae);
    printf("\033[1;35m╚══════════════════════════════════════════════════════╝\033[0m\n\n");
    return true;
}

void taskAnomalyDetection(void *pvParameters) {
    if (!setup_tinyml()) {
        ESP_LOGE(TAG, "Setup failed, terminating task.");
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        SampleData sd;

        // 1. Wait for fresh data from Queue (Blocking)
        if (xQueueReceive(sensorQueue, &sd, pdMS_TO_TICKS(10000)) == pdTRUE) {
            float current_temp = sd.temp;
            float current_hum  = sd.hum;

            ESP_LOGI(TAG, "New data received from Queue: T=%.2f, H=%.2f", current_temp, current_hum);

            // 2. Add to window buffer (sliding window)
            window_buffer[window_idx][0] = current_temp;
            window_buffer[window_idx][1] = current_hum;
            window_idx++;
            inference_stride_counter++;

            if (window_idx >= WINDOW_SIZE) {
                for (int i = 0; i < WINDOW_SIZE - 1; i++) {
                    window_buffer[i][0] = window_buffer[i + 1][0];
                    window_buffer[i][1] = window_buffer[i + 1][1];
                }

                window_buffer[WINDOW_SIZE - 1][0] = current_temp;
                window_buffer[WINDOW_SIZE - 1][1] = current_hum;
                window_idx = WINDOW_SIZE;

                if (!window_full) {
                    ESP_LOGI(TAG, "Window buffer is now FULL (%d/%d). Starting inference cycles.", window_idx, WINDOW_SIZE);
                    window_full = true;
                }
            } else {
                static uint8_t fill_log_throttle = 0;
                if (fill_log_throttle++ % 5 == 0) { // Log every 5 samples
                    ESP_LOGI(TAG, "Filling window... (%d/%d)", window_idx, WINDOW_SIZE);
                }
            }

            // 3. Run inference
            if (window_full && (inference_stride_counter % AI_INFERENCE_STRIDE == 0)) {
                ESP_LOGI(TAG, "--- Starting TinyML Inference (Stride: %d) ---", AI_INFERENCE_STRIDE);

                float shift_temp = stats.runtime_mean_temp - MODEL_TRAINING_MEAN_TEMP;
                float shift_hum  = stats.runtime_mean_hum  - MODEL_TRAINING_MEAN_HUM;

                float shifted_window[AI_WINDOW_SIZE][AI_NUM_FEATURES];
                for (int i = 0; i < AI_WINDOW_SIZE; i++) {
                    shifted_window[i][0] = window_buffer[i][0] - shift_temp;
                    shifted_window[i][1] = window_buffer[i][1] - shift_hum;
                }

                int64_t start_time = esp_timer_get_time();
                float mae = run_autoencoder_inference(shifted_window);
                int64_t end_time = esp_timer_get_time();

                if (mae >= 0) {
                    pico.last_latency_us = end_time - start_time;
                    pico.inference_count++;
                    pico.cpu_load = ((float)pico.last_latency_us / 2000000.0f) * 100.0f;

                    float dynamic_threshold = calculate_dynamic_threshold();
                    bool is_anomaly = (mae > dynamic_threshold);
                    bool is_trend_anomaly = false;

                    if (!is_anomaly) {
                        is_trend_anomaly = detect_trend_anomaly(mae);
                        is_anomaly = is_trend_anomaly;
                    }

                    // ⚠️ PHYSICAL SAFETY OVERRIDE: Force anomaly on dangerous physical readings
                    // This prevents False Negatives when the model hasn't adapted to extreme conditions
                    bool is_physical_override = false;
                    if (!is_anomaly) {
                        if (current_temp > ENV_TEMP_MAX || current_temp < ENV_TEMP_MIN ||
                            current_hum > ENV_HUM_MAX   || current_hum < ENV_HUM_MIN) {
                            is_anomaly = true;
                            is_physical_override = true;
                            ESP_LOGW(TAG, "[PHYSICAL OVERRIDE] T=%.2f H=%.2f exceeds safe range!", current_temp, current_hum);
                        }
                    }

                    prev_mae = mae;

                    update_welford_stats(mae, current_temp, current_hum);
                    apply_rl_feedback(mae, current_temp, is_anomaly);

                    // Print training/adaptation update every 20 inferences
                    if (pico.inference_count % 20 == 0) {
                        float variance = (stats.count > 1) ? (stats.M2_mae / stats.count) : 0.0f;
                        float std_dev = sqrtf(variance);
                        printf("\n\033[1;33m┌─── UPDATE MODEL ONLINE (Inference #%lu) ─────────────┐\033[0m\n", pico.inference_count);
                        printf("\033[1;33m│  Samples belajar : %lu                               │\033[0m\n", stats.count);
                        printf("\033[1;33m│  Mean MAE        : %.4f                          │\033[0m\n", stats.mean_mae);
                        printf("\033[1;33m│  Std Dev MAE     : %.4f                          │\033[0m\n", std_dev);
                        printf("\033[1;33m│  Threshold saat ini: %.4f                        │\033[0m\n", calculate_dynamic_threshold());
                        printf("\033[1;33m│  RL Multiplier   : %.2f                            │\033[0m\n", current_policy_threshold_multiplier);
                        printf("\033[1;33m│  Runtime Mean T  : %.2f°C  H: %.2f%%              │\033[0m\n", stats.runtime_mean_temp, stats.runtime_mean_hum);
                        printf("\033[1;33m└──────────────────────────────────────────────────────┘\033[0m\n\n");
                    }

                    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        dataKandang.anomaly_detected = is_anomaly;
                        dataKandang.mae              = mae;
                        dataKandang.inference_count  = (uint32_t)pico.inference_count;
                        dataKandang.latency_us       = (uint32_t)pico.last_latency_us;
                        dataKandang.cpu_load         = pico.cpu_load;
                        dataKandang.memory_used      = 64 * 1024;
                        xSemaphoreGive(dataMutex);
                    }

                    // --- Pretty Table Row ---
                    time_t now;
                    time(&now);
                    struct tm timeinfo;
                    localtime_r(&now, &timeinfo);
                    char ts[24];
                    strftime(ts, sizeof(ts), "%Y/%m/%d %H:%M:%S", &timeinfo);

                    static bool header_printed = false;
                    if (!header_printed) {
                        printf("\n\033[1;36m========================================================================\033[0m\n");
                        printf("\033[1;36m| %-20s | %-7s | %-7s | %-12s | %-8s |\033[0m\n",
                               "WAKTU (NTP)", "TEMP", "HUM", "STATUS", "MAE");
                        printf("\033[1;36m========================================================================\033[0m\n");
                        header_printed = true;
                    }

                    if (is_anomaly) {
                        const char* status_label = is_physical_override ? "BAHAYA!(Fisik)" :
                                                   (is_trend_anomaly    ? "ANOMALI(Trend)" : "ANOMALI!");
                        printf("\033[1;31m| %-20s | %7.2f | %7.2f | %-14s | %8.4f |\033[0m\n",
                               ts, current_temp, current_hum, status_label, mae);
                    } else {
                        printf("\033[0;32m| %-20s | %7.2f | %7.2f | %-14s | %8.4f |\033[0m\n",
                               ts, current_temp, current_hum, "NORMAL", mae);
                    }

                    mqtt_publish_sensor_data();
                }

                vTaskDelay(pdMS_TO_TICKS(200));
            }

        } else {
            // If no data received, still publish
            mqtt_publish_sensor_data();
        }

        // IMPORTANT: delay harus di dalam loop
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}


