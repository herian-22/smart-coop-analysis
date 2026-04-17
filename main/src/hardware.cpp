/**
 * @file hardware.cpp
 * @brief I2C, SHT31, PCF8574, LCD, Buzzer initialization and tasks.
 *
 * Tasks:
 *   taskUpdateLCD  — 100ms cycle, renders face animation
 *   taskReadSHT    — 2s cycle, reads temperature → updates DisplayMode
 *   taskBuzzer     — Queue-driven, plays tones via LEDC PWM
 */

#include "hardware.h"
#include "app_config.h"
#include "lcd_face.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include <pcf8574.h>
#include <hd44780.h>
#include <sht3x.h>
#include <i2cdev.h>

static const char *TAG = "Hardware";

static SemaphoreHandle_t i2cMutex = NULL;
static i2c_dev_t pcf8574_dev;
static hd44780_t lcd;
static sht3x_t sensor_sht;

// --- Buzzer ---
static QueueHandle_t s_buzzer_queue = NULL;

// =========================================================================
// Buzzer (LEDC PWM on GPIO 33)
// =========================================================================

static void play_tone(uint32_t freq_hz, uint32_t duration_ms)
{
    ledc_set_freq(LEDC_HIGH_SPEED_MODE, LEDC_TIMER_0, freq_hz);
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, 512);  // 50% duty
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, 0);    // Off
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
}

void buzzer_init(void)
{
    ledc_timer_config_t timer_conf = {};
    timer_conf.speed_mode      = LEDC_HIGH_SPEED_MODE;
    timer_conf.duty_resolution = LEDC_TIMER_10_BIT;
    timer_conf.timer_num       = LEDC_TIMER_0;
    timer_conf.freq_hz         = 2000;
    timer_conf.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t ch_conf = {};
    ch_conf.gpio_num   = BUZZER_GPIO_NUM;
    ch_conf.speed_mode = LEDC_HIGH_SPEED_MODE;
    ch_conf.channel    = LEDC_CHANNEL_0;
    ch_conf.intr_type  = LEDC_INTR_DISABLE;
    ch_conf.timer_sel  = LEDC_TIMER_0;
    ch_conf.duty       = 0;
    ch_conf.hpoint     = 0;
    ledc_channel_config(&ch_conf);

    s_buzzer_queue = xQueueCreate(5, sizeof(buzzer_cmd_t));
    ESP_LOGI(TAG, "Buzzer initialized on GPIO %d", BUZZER_GPIO_NUM);
}

void buzzer_play(buzzer_cmd_t cmd)
{
    if (s_buzzer_queue != NULL) {
        xQueueSend(s_buzzer_queue, &cmd, 0);
    }
}

void taskBuzzer(void *pvParameters)
{
    buzzer_cmd_t cmd;
    for (;;) {
        if (xQueueReceive(s_buzzer_queue, &cmd, portMAX_DELAY)) {
            switch (cmd) {
                case BUZZER_CMD_BOOT:
                    play_tone(1500, 100);
                    vTaskDelay(pdMS_TO_TICKS(30));
                    play_tone(2000, 100);
                    vTaskDelay(pdMS_TO_TICKS(30));
                    play_tone(2500, 150);
                    break;

                case BUZZER_CMD_STATE_CHANGE:
                    play_tone(2500, 80);
                    vTaskDelay(pdMS_TO_TICKS(50));
                    play_tone(1800, 120);
                    break;

                case BUZZER_CMD_ALARM_HOT:
                    for (int i = 0; i < 3; i++) {
                        play_tone(3000, 100);
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                    break;
            }
        }
    }
}

// =========================================================================
// I2C Bus Recovery
// =========================================================================
static void i2c_bus_reset(void) {
    ESP_LOGW(TAG, "Memulai pemulihan Bus I2C...");

    sensor_sht.i2c_dev.dev_handle = NULL;
    pcf8574_dev.dev_handle = NULL;
    vTaskDelay(pdMS_TO_TICKS(50));

    gpio_reset_pin(I2C_SCL_GPIO);
    gpio_reset_pin(I2C_SDA_GPIO);

    gpio_config_t reset_conf = {};
    reset_conf.mode = GPIO_MODE_OUTPUT_OD;
    reset_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    reset_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    reset_conf.pin_bit_mask = (1ULL << I2C_SCL_GPIO) | (1ULL << I2C_SDA_GPIO);
    gpio_config(&reset_conf);

    gpio_set_level(I2C_SDA_GPIO, 1);
    for (int i = 0; i < 9; i++) {
        gpio_set_level(I2C_SCL_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(5));
        gpio_set_level(I2C_SCL_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    gpio_set_level(I2C_SCL_GPIO, 1);
    gpio_set_level(I2C_SDA_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(I2C_SDA_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(20));

    i2cdev_init();

    sht3x_init_desc(&sensor_sht, SHT3X_I2C_ADDR_GND, I2C_NUM_0, I2C_SDA_GPIO, I2C_SCL_GPIO);
    sensor_sht.i2c_dev.cfg.master.clk_speed = 100000;
    sht3x_init(&sensor_sht);

    pcf8574_init_desc(&pcf8574_dev, 0x27, I2C_NUM_0, I2C_SDA_GPIO, I2C_SCL_GPIO);
    pcf8574_dev.cfg.master.clk_speed = 100000;
    hd44780_init(&lcd);
    hd44780_switch_backlight(&lcd, true);

    ESP_LOGI(TAG, "Pemulihan I2C selesai.");
}

// =========================================================================
// Hardware Init
// =========================================================================
esp_err_t hw_init(void) {
    esp_err_t res;

    i2cMutex = xSemaphoreCreateMutex();
    if (i2cMutex == NULL) {
        ESP_LOGE(TAG, "Failed to create I2C mutex");
        return ESP_FAIL;
    }

    res = i2cdev_init();
    if (res != ESP_OK) return res;

    // --- SHT31 Sensor (address 0x44) ---
    sht3x_init_desc(&sensor_sht, SHT3X_I2C_ADDR_GND, I2C_NUM_0, I2C_SDA_GPIO, I2C_SCL_GPIO);
    sensor_sht.i2c_dev.cfg.master.clk_speed = 100000;
    res = sht3x_init(&sensor_sht);
    if (res != ESP_OK) {
        ESP_LOGW(TAG, "SHT3x init failed (will retry in task): %s", esp_err_to_name(res));
    } else {
        ESP_LOGI(TAG, "SHT3x sensor initialized");
    }

    // --- LCD via PCF8574 (address 0x27) ---
    pcf8574_init_desc(&pcf8574_dev, 0x27, I2C_NUM_0, I2C_SDA_GPIO, I2C_SCL_GPIO);
    pcf8574_dev.cfg.master.clk_speed = 100000;

    lcd.write_cb = [](const hd44780_t *l, uint8_t d) { return pcf8574_port_write(&pcf8574_dev, d); };
    lcd.font = HD44780_FONT_5X8;
    lcd.lines = 2;
    lcd.pins.rs = 0; lcd.pins.e = 2; lcd.pins.d4 = 4; lcd.pins.d5 = 5; lcd.pins.d6 = 6; lcd.pins.d7 = 7; lcd.pins.bl = 3;

    res = hd44780_init(&lcd);
    if (res == ESP_OK) {
        hd44780_switch_backlight(&lcd, true);
    } else {
        ESP_LOGE(TAG, "Failed to initialize LCD");
    }

    // --- Face Animation (CGRAM upload) ---
    if (res == ESP_OK) {
        esp_err_t face_ret = face_anim_init(&lcd, i2cMutex);
        if (face_ret != ESP_OK) {
            ESP_LOGW(TAG, "Face animation init failed: %s", esp_err_to_name(face_ret));
        }
    }

    return ESP_OK;
}

hd44780_t* hw_get_lcd(void) { return &lcd; }
SemaphoreHandle_t hw_get_i2c_mutex(void) { return i2cMutex; }

// =========================================================================
// Task: Read SHT31 sensor → update DisplayMode + trigger buzzer
// =========================================================================
void taskReadSHT(void *pvParameters) {
    ESP_LOGI(TAG, "SHT31 sensor task started");
    int fail_count = 0;
    bool hot_alarm_sent = false;   // Prevent spamming alarm

    vTaskDelay(pdMS_TO_TICKS(1000));

    for (;;) {
        float t = 0, h = 0;

        if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            esp_err_t sht_res = ESP_FAIL;

            esp_err_t probe_res = sht3x_measure(&sensor_sht, &t, &h);

            if (probe_res == ESP_OK) {
                if (t < -40.0f || t > 125.0f || h < 0.0f || h > 100.0f ||
                    t != t || h != h) {
                    ESP_LOGW(TAG, "SHT3x data out of range (%.1f°C, %.1f%%) — sensor possibly disconnected", t, h);
                    sht_res = ESP_ERR_INVALID_RESPONSE;
                } else {
                    sht_res = ESP_OK;
                }
            } else {
                for (int retry = 1; retry < 3; retry++) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                    sht_res = sht3x_measure(&sensor_sht, &t, &h);
                    if (sht_res == ESP_OK) break;
                    ESP_LOGW(TAG, "SHT3x read error (try %d/3): %s", retry + 1, esp_err_to_name(sht_res));
                }
            }

            if (sht_res != ESP_OK) {
                fail_count++;
                ESP_LOGE(TAG, "SHT3x failure #%d/%d", fail_count, SHT_FAIL_THRESHOLD);

                if (fail_count >= SHT_FAIL_THRESHOLD) {
                    if (currentDisplayMode != DISPLAY_MODE_ERROR) {
                        ESP_LOGE(TAG, "Sensor offline → DISPLAY_MODE_ERROR");
                        currentDisplayMode = DISPLAY_MODE_ERROR;
                        face_set_display_mode(DISPLAY_MODE_ERROR);
                        buzzer_play(BUZZER_CMD_STATE_CHANGE);
                    }
                    if (fail_count % SHT_FAIL_THRESHOLD == 0) {
                        i2c_bus_reset();
                    }
                }
            }

            xSemaphoreGive(i2cMutex);

            if (sht_res == ESP_OK) {
                if (fail_count > 0) {
                    ESP_LOGI(TAG, "SHT3x recovered after %d failures", fail_count);
                    fail_count = 0;
                }

                if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    sensorData.temp = t;
                    sensorData.hum  = h;
                    xSemaphoreGive(dataMutex);
                }

                ESP_LOGI(TAG, "Temp: %.1f°C  Hum: %.1f%%", t, h);

                // --- Determine display mode with hysteresis ---
                DisplayMode current = currentDisplayMode;
                DisplayMode next = current;

                if (current == DISPLAY_MODE_ERROR) {
                    next = DISPLAY_MODE_NORMAL;
                } else if (current == DISPLAY_MODE_NORMAL) {
                    if (t < TEMP_COLD_THRESHOLD)  next = DISPLAY_MODE_COLD;
                    if (t > TEMP_HOT_THRESHOLD)   next = DISPLAY_MODE_HOT;
                } else if (current == DISPLAY_MODE_COLD) {
                    if (t >= TEMP_COLD_THRESHOLD + TEMP_HYSTERESIS) next = DISPLAY_MODE_NORMAL;
                    if (t > TEMP_HOT_THRESHOLD) next = DISPLAY_MODE_HOT;
                } else if (current == DISPLAY_MODE_HOT) {
                    if (t <= TEMP_HOT_THRESHOLD - TEMP_HYSTERESIS) next = DISPLAY_MODE_NORMAL;
                    if (t < TEMP_COLD_THRESHOLD) next = DISPLAY_MODE_COLD;
                }

                if (next != current) {
                    const char *mode_names[] = {"NORMAL", "COLD", "HOT", "ERROR"};
                    ESP_LOGI(TAG, "Mode: %s -> %s (%.1f°C)",
                             mode_names[current], mode_names[next], t);
                    currentDisplayMode = next;
                    face_set_display_mode(next);
                    buzzer_play(BUZZER_CMD_STATE_CHANGE);
                    hot_alarm_sent = false;  // Reset alarm on mode change
                }

                // --- HOT alarm: single trigger when T > 35°C ---
                if (current == DISPLAY_MODE_HOT && t > 35.0f && !hot_alarm_sent) {
                    buzzer_play(BUZZER_CMD_ALARM_HOT);
                    hot_alarm_sent = true;
                }
                if (t <= 35.0f) {
                    hot_alarm_sent = false;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// =========================================================================
// Task: LCD face animation renderer
// =========================================================================
void taskUpdateLCD(void *pvParameters) {
    for (;;) {
        if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (currentState) {
                case BOOTING:
                    hd44780_clear(&lcd);
                    hd44780_puts(&lcd, "Inisialisasi...");
                    break;

                case FACE_ANIM:
                    face_anim_draw_frame();
                    break;
            }
            xSemaphoreGive(i2cMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
