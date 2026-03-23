#include "hardware.h"
#include "app_config.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include <sht3x.h>
#include <pcf8574.h>
#include <hd44780.h>
#include <i2cdev.h>
#include <time.h>

static const char *TAG = "Hardware";

static SemaphoreHandle_t i2cMutex = NULL;
static sht3x_t sensor_sht;
static i2c_dev_t pcf8574_dev;
static hd44780_t lcd;
static bool header_printed = false;

/**
 * @brief Perform a software I2C bus reset by bit-banging 9 clock pulses.
 *
 * When a power glitch (e.g., from WiFi bursts) causes an I2C slave to hold
 * SDA low, the bus stalls permanently. This function releases it by:
 * 1. Toggling SCL 9 times to satisfy any in-progress slave transaction.
 * 2. Sending a STOP condition (SDA rising while SCL is high).
 * 3. Releasing GPIO pins back to floating input so the I2C master driver
 *    can reclaim them cleanly — this prevents "GPIO not usable" warnings.
 * 4. Nulling device handles so i2cdev re-adds them on the next access.
 */

 /**
 * @brief Implementasi Pemulihan I2C Standar Industri.
 * Memastikan driver dihapus total sebelum manipulasi GPIO manual.
 */
static void i2c_bus_reset(void) {
    ESP_LOGW(TAG, "Memulai pemulihan Bus I2C (Mode Kompatibilitas ESP-IDF 5.x)...");
    
    // Jangan panggil i2c_driver_delete untuk menghindari konflik "driver_ng"
    // Kita langsung "ambil alih" pin secara paksa lewat reset_pin
    
    // Set handle ke NULL untuk mencegah akses tidak sengaja saat recovery
    sensor_sht.i2c_dev.dev_handle = NULL;
    pcf8574_dev.dev_handle = NULL;

    vTaskDelay(pdMS_TO_TICKS(50));

    // 2. Reset Pin secara total agar tidak ada peripheral yang tersangkut
    gpio_reset_pin(I2C_SCL_GPIO);
    gpio_reset_pin(I2C_SDA_GPIO);

    // 3. Konfigurasi eksplisit untuk bit-banging (Open Drain + Pullup)
    gpio_config_t reset_conf = {};
    reset_conf.mode = GPIO_MODE_OUTPUT_OD;
    reset_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    reset_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    reset_conf.pin_bit_mask = (1ULL << I2C_SCL_GPIO) | (1ULL << I2C_SDA_GPIO);
    gpio_config(&reset_conf);

    // 4. Kirim 9 Pulsa SCL untuk memaksa Slave (SHT/LCD) melepaskan jalur SDA
    gpio_set_level(I2C_SDA_GPIO, 1);
    for (int i = 0; i < 9; i++) {
        gpio_set_level(I2C_SCL_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(5));
        gpio_set_level(I2C_SCL_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // 5. Generate kondisi STOP secara manual
    gpio_set_level(I2C_SCL_GPIO, 1);
    gpio_set_level(I2C_SDA_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(I2C_SDA_GPIO, 1); // SDA naik saat SCL tinggi = STOP
    vTaskDelay(pdMS_TO_TICKS(20));

    // 6. Inisialisasi ulang Driver I2C dan Perangkat dari nol
    // Panggil i2cdev_init() untuk memastikan layer abstraksi siap
    i2cdev_init();

    // Re-inisialisasi SHT3x
    sht3x_init_desc(&sensor_sht, SHT3X_I2C_ADDR_GND, I2C_NUM_0, I2C_SDA_GPIO, I2C_SCL_GPIO);
    sensor_sht.i2c_dev.cfg.master.clk_speed = 100000; // Kecepatan aman 100kHz
    sht3x_init(&sensor_sht);

    // Re-inisialisasi LCD
    pcf8574_init_desc(&pcf8574_dev, 0x27, I2C_NUM_0, I2C_SDA_GPIO, I2C_SCL_GPIO);
    pcf8574_dev.cfg.master.clk_speed = 100000;
    hd44780_init(&lcd);
    hd44780_switch_backlight(&lcd, true);

    ESP_LOGI(TAG, "Pemulihan I2C Selesai. Driver dan Perangkat dipasang ulang.");
}


esp_err_t hw_init(void) {
    esp_err_t res;
    
    // Create mutex for I2C bus sharing
    i2cMutex = xSemaphoreCreateMutex();
    if (i2cMutex == NULL) {
        ESP_LOGE(TAG, "Failed to create I2C mutex");
        return ESP_FAIL;
    }
    
    // Initialize I2C driver
    res = i2cdev_init();
    if (res != ESP_OK) return res;

    // SHT3x Sensor Initialization
    sht3x_init_desc(&sensor_sht, SHT3X_I2C_ADDR_GND, I2C_NUM_0, I2C_SDA_GPIO, I2C_SCL_GPIO);
    sensor_sht.i2c_dev.cfg.master.clk_speed = 100000; // Force 100kHz (Override library 1MHz default)
    res = sht3x_init(&sensor_sht);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SHT3x sensor");
    }

    // LCD via PCF8574 Initialization
    pcf8574_init_desc(&pcf8574_dev, 0x27, I2C_NUM_0, I2C_SDA_GPIO, I2C_SCL_GPIO);
    pcf8574_dev.cfg.master.clk_speed = 100000; // Force 100kHz
    
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

    return ESP_OK;
}

void taskReadSHT(void *pvParameters) {
    for (;;) {
        // Wait for system to be running (connected to WiFi)
        xEventGroupWaitBits(system_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
        
        float t, h;
            if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                // SHT3x measurement with retry logic for CRC failures
                int sht_retry = 0;
                esp_err_t sht_res = ESP_FAIL;
                while (sht_retry < 3) {
                    sht_res = sht3x_measure(&sensor_sht, &t, &h);
                    if (sht_res == ESP_OK) break;
                    
                    sht_retry++;
                    ESP_LOGW(TAG, "SHT3x Read Error (Try %d/3): %s", sht_retry, esp_err_to_name(sht_res));
                    vTaskDelay(pdMS_TO_TICKS(200)); 
                }

                if (sht_res != ESP_OK) {
                    ESP_LOGE(TAG, "Persistent SHT3x failure - attempting I2C bus recovery");
                    i2c_bus_reset(); // Manual bit-bang reset
                }

                if (sht_res == ESP_OK) {
                    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        dataKandang.temp = t;
                        dataKandang.hum = h;
                        
                        time_t now;
                        struct tm timeinfo;
                        time(&now);
                        localtime_r(&now, &timeinfo);

                        // Only write standard timestamp if synced proper (year > 2020)
                         if (timeinfo.tm_year > (2020 - 1900)) {
                            strftime(dataKandang.date_str, sizeof(dataKandang.date_str), "%Y/%m/%d %H:%M:%S", &timeinfo);
                            
                            if (!header_printed) {
                                printf("\n--- START CSV DATA ---\n");
                                printf("Timestamp,Temperature,Humidity,Anomaly\n");
                                header_printed = true;
                            }
                            printf("%s,%.2f,%.2f,%d\n", dataKandang.date_str, t, h, dataKandang.anomaly_detected ? 1 : 0);
                        }
                        xSemaphoreGive(dataMutex);

                        // [NEW] Send to Queue for TinyML Task
                        SampleData sd = { .temp = t, .hum = h };
                        if (xQueueSend(sensorQueue, &sd, 0) != pdTRUE) {
                            ESP_LOGW(TAG, "Sensor Queue Full (TinyML task might be slow)");
                        }
                    }
                } else {
                    ESP_LOGE(TAG, "Failed reading sensor SHT3x - attempting I2C bus recovery");
                    i2c_bus_reset(); // Re-enabled with new pin logic
                }
                xSemaphoreGive(i2cMutex);
            }
            // Delay moved INSIDE the for(;;) loop
            
            // --- STACK TUNING MONITOR ---
            static uint8_t stack_log_throttle_sht = 0;
            if (stack_log_throttle_sht++ % 10 == 0) {
                ESP_LOGI(TAG, "[STACK TUNING] %s High Water Mark: %u bytes free", pcTaskGetName(NULL), uxTaskGetStackHighWaterMark(NULL));
            }

            vTaskDelay(pdMS_TO_TICKS(2000));
        }
}

// [NEW] Placeholder for "opam" (Operational Amplifier) / Analog Sensor
// Most SmartCoop designs use an OP-AMP for pH or CO2 analog sensors
void taskReadAnalog(void *pvParameters) {
    for (;;) {
        // Implement ADC read here if "opam" refers to an analog amplifier circuit
        // float raw_adc = adc1_get_raw(ADC1_CHANNEL_X); 
        vTaskDelay(pdMS_TO_TICKS(5000)); 
    }
}

void taskUpdateLCD(void *pvParameters) {
    char l0[17], l1[17];
    for (;;) {
        // Wait for connection bits but with a timeout to allow booting UI to show
        xEventGroupWaitBits(system_event_group, WIFI_CONNECTED_BIT | MQTT_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(1000));

        if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            hd44780_clear(&lcd);
            switch (currentState) {
                case BOOTING:
                    hd44780_puts(&lcd, "Inisialisasi...");
                    break;
                case CONFIG_WIFI:
                    hd44780_gotoxy(&lcd, 0, 0); hd44780_puts(&lcd, "Config WiFi...");
                    hd44780_gotoxy(&lcd, 0, 1); hd44780_puts(&lcd, "SSID: SmartCoop");
                    break;
                case CONNECTED:
                    hd44780_gotoxy(&lcd, 0, 0); hd44780_puts(&lcd, "Connected To:");
                    hd44780_gotoxy(&lcd, 0, 1); hd44780_puts(&lcd, dataKandang.connected_ssid);
                    break;
                case RUNNING:
                    // Take dataMutex to read data safely
                    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                        snprintf(l0, sizeof(l0), "T:%.1f C H:%.1f%%", dataKandang.temp, dataKandang.hum);
                        
                        if (dataKandang.anomaly_detected) {
                            snprintf(l1, sizeof(l1), "STATUS: ANOMALI!");
                        } else {
                            snprintf(l1, sizeof(l1), "%.14s", dataKandang.date_str);
                        }
                        
                        xSemaphoreGive(dataMutex);
                    }
                    
                    hd44780_gotoxy(&lcd, 0, 0);
                    hd44780_puts(&lcd, l0);
                    hd44780_gotoxy(&lcd, 0, 1);
                    hd44780_puts(&lcd, l1);
                    break;
            }

            xSemaphoreGive(i2cMutex);
        }

        // --- STACK TUNING MONITOR ---
        static uint8_t stack_log_throttle_lcd = 0;
        if (stack_log_throttle_lcd++ % 10 == 0) {
            ESP_LOGI(TAG, "[STACK TUNING] %s High Water Mark: %u bytes free", pcTaskGetName(NULL), uxTaskGetStackHighWaterMark(NULL));
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
