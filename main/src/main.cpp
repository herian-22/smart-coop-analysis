#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_sntp.h"

// --- Custom Modules ---
#include "app_config.h"
#include "hardware.h"
#include "web_server.h"
#include "mqtt_handler.h"
#include "tinyml_task.h"

static const char *TAG = "SmartCoop_Main";

// Global Variables Definition
SystemState currentState = BOOTING;
SensorData dataKandang;
SemaphoreHandle_t dataMutex = NULL;
EventGroupHandle_t system_event_group = NULL;
QueueHandle_t sensorQueue = NULL;

// --- 1. NTP Sync Module ---
bool init_sntp() {
    ESP_LOGI(TAG, "Sinkronisasi Waktu (NTP)...");
    if (esp_sntp_enabled()) esp_sntp_stop();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "129.250.35.250"); // [IP] NTT pool
    esp_sntp_setservername(2, "162.159.200.1");   // [IP] Cloudflare NTP
    esp_sntp_setservername(3, "time.google.com"); 
    esp_sntp_init();

    // Wait for time to be set (Epoch > 1.6B - Year 2020+)
    int retry = 0;
    const int retry_count = 30; // 30s timeout per attempt
    time_t now = 0;
    struct tm timeinfo = { 0 };

    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Menunggu sinkronisasi waktu... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(1000));
        time(&now);
        if (now > 1600000000) break; 
    }

    if (now < 1600000000) {
        ESP_LOGE(TAG, "Gagal sinkronisasi waktu (NTP Timeout atau Invalid Epoch: %lld)", (long long)now);
        return false;
    }

    setenv("TZ", "WIB-7", 1);
    tzset();
    
    localtime_r(&now, &timeinfo);
    ESP_LOGI(TAG, "Waktu tersinkronisasi (Epoch: %lld): %s", (long long)now, asctime(&timeinfo));
    return true;
}

// --- 2. Network Stack Task (WiFi -> NTP bg -> MQTT) ---
void taskNetworkStack(void *pvParameters) {
    ESP_LOGI(TAG, "Network Stack Task Started");

    // Step A: Wait for WiFi
    xEventGroupWaitBits(system_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    // Step B: Web Server
    web_server_start();

    // Step C: NTP fires in background - no blocking, SNTP will auto-sync when ready
    if (esp_sntp_enabled()) esp_sntp_stop();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "129.250.35.250"); // Cloudflare NTP IP
    esp_sntp_setservername(2, "162.159.200.1");  // NTT NTP IP
    esp_sntp_init();
    setenv("TZ", "WIB-7", 1);
    tzset();
    ESP_LOGI(TAG, "NTP started (background sync)");

    // Stabilization delay: allow I2C bus and CPU to settle after WiFi burst
    vTaskDelay(pdMS_TO_TICKS(500));

    // Step D: MQTT starts immediately, no waiting for NTP
    mqtt_init();

    currentState = RUNNING;
    ESP_LOGI(TAG, "Network Stack Ready (MQTT up, NTP syncing in background)");
    vTaskDelete(NULL);
}

// --- 3. WiFi Event Handler ---
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi Terputus, mencoba lagi...");
        esp_wifi_connect();
        xEventGroupClearBits(system_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Dapat IP: " IPSTR, IP2STR(&event->ip_info.ip));
        
        xEventGroupSetBits(system_event_group, WIFI_CONNECTED_BIT);
        currentState = CONNECTED;
    }
}

extern "C" void app_main() {
    // 1. Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize Shared Data Resources
    dataMutex = xSemaphoreCreateMutex();
    system_event_group = xEventGroupCreate();
    sensorQueue = xQueueCreate(30, sizeof(SampleData)); // Increased to 30 for stability

    // 3. Initialize Hardware Modules (I2C, SHT3x, PCF8574, LCD)
    ESP_ERROR_CHECK(hw_init());

    // 4. Start Base RTOS Tasks
    xTaskCreate(taskUpdateLCD, "LCD", 8192, NULL, 2, NULL);
    xTaskCreate(taskReadSHT, "SHT", 8192, NULL, 3, NULL);
    xTaskCreate(taskReadAnalog, "Analog", 4096, NULL, 1, NULL); 
    xTaskCreate(taskAnomalyDetection, "TinyML", 16383, NULL, 2, NULL); // Priority 2 - yields to MQTT
    xTaskCreate(taskNetworkStack, "Network", 8192, NULL, 5, NULL);     // Priority 5 - highest, ensures MQTT ping response

    // 5. Initialize Networking & WiFi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_FLASH);

    wifi_config_t sta_conf;
    if (esp_wifi_get_config(WIFI_IF_STA, &sta_conf) == ESP_OK && strlen((char *)sta_conf.sta.ssid) > 0) {
        currentState = CONNECTED;
        strcpy(dataKandang.connected_ssid, (char *)sta_conf.sta.ssid);
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
        esp_wifi_set_ps(WIFI_PS_NONE); 
    } else {
        currentState = CONFIG_WIFI;
        wifi_config_t ap_conf = {};
        strcpy((char *)ap_conf.ap.ssid, "SmartCoop_Config");
        ap_conf.ap.authmode = WIFI_AUTH_OPEN;
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        esp_wifi_set_config(WIFI_IF_AP, &ap_conf);
        esp_wifi_start();
        esp_wifi_set_ps(WIFI_PS_NONE); 
        web_server_start();
    }

    // 6. Configure Power Management (Debug: DFS only, no auto-sleep)
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 240,
        .min_freq_mhz = 40,
        .light_sleep_enable = false
    };
    esp_err_t pm_err = esp_pm_configure(&pm_config);
    if (pm_err == ESP_OK) {
        ESP_LOGI(TAG, "Power Management Configured (DFS Only)");
    }

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}