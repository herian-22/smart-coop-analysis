#include "mqtt_handler.h"
#include "app_config.h"

#include "mqtt_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include <sys/time.h>
#include "esp_mac.h"

static const char *TAG = "MQTT_Handler";
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool is_mqtt_connected = false;

// TODO: Replace with your broker URI
#define BROKER_URI "mqtt://43.133.54.55:1883"  // jesio.site - hardcoded IP to bypass DNS
#define TOPIC_PUBLISH "smartcoop/sensor"
#define TOPIC_ANOMALY "smartcoop/anomaly"

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            is_mqtt_connected = true;
            if (system_event_group) {
                xEventGroupSetBits(system_event_group, MQTT_CONNECTED_BIT);
            }
            printf("\n\033[1;32m╔══════════════════════════════════════════════════════╗\033[0m\n");
            printf("\033[1;32m║          MQTT BROKER TERHUBUNG ✓                     ║\033[0m\n");
            printf("\033[1;32m╠══════════════════════════════════════════════════════╣\033[0m\n");
            printf("\033[1;32m║  Broker : %s\033[0m\n", BROKER_URI);
            printf("\033[1;32m║  Topic  : smartcoop/sensor\033[0m\n");
            printf("\033[1;32m║  Format Kirim (JSON):\033[0m\n");
            printf("\033[1;32m║  {\033[0m\n");
            printf("\033[1;32m║    \"temperature\": 30.67,\033[0m\n");
            printf("\033[1;32m║    \"humidity\":    82.49,\033[0m\n");
            printf("\033[1;32m║    \"timestamp\":  \"2026/03/23 04:00:01\",\033[0m\n");
            printf("\033[1;32m║    \"anomaly\":    false,\033[0m\n");
            printf("\033[1;32m║    \"mae\":         0.0806,\033[0m\n");
            printf("\033[1;32m║    \"latency_us\": 18500,\033[0m\n");
            printf("\033[1;32m║    \"epoch_ms\":   1742745301000\033[0m\n");
            printf("\033[1;32m║  }\033[0m\n");
            printf("\033[1;32m╚══════════════════════════════════════════════════════╝\033[0m\n\n");
            break;
        case MQTT_EVENT_DISCONNECTED:
            is_mqtt_connected = false;
            if (system_event_group) {
                xEventGroupClearBits(system_event_group, MQTT_CONNECTED_BIT);
            }
            printf("\033[1;33m[MQTT] Terputus. Menunggu reconnect...\033[0m\n");
            break;
        case MQTT_EVENT_PUBLISHED:
            // ESP_LOGI(TAG, "MQTT Published, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT Error");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "TCP Transport error: %s", strerror(event->error_handle->esp_transport_sock_errno));
            }
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
}

void mqtt_init(void) {
    // Optimized MQTT Config for ESP-IDF 5.x
    esp_mqtt_client_config_t mqtt_cfg = {};
    
    // Broker configuration
    mqtt_cfg.broker.address.uri = BROKER_URI;
    
    // Credentials
    mqtt_cfg.credentials.username = "zyramqtt";
    mqtt_cfg.credentials.authentication.password = "Jefri@27";
    
    // Generate Unique Client ID using MAC Address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    static char static_client_id[32]; 
    snprintf(static_client_id, sizeof(static_client_id), "SmartCoop%02X%02X%02X", mac[3], mac[4], mac[5]);
    mqtt_cfg.credentials.client_id = static_client_id;
    
    // Connection & Performance Tuning
    mqtt_cfg.session.keepalive = 30;              // Standard keepalive
    mqtt_cfg.session.disable_clean_session = false; // Start fresh session
    
    mqtt_cfg.outbox.limit = 20;                   // Correct path for outbox limit
    
    mqtt_cfg.task.priority = 6;                   // [BOOSTED] Higher than all other tasks
    mqtt_cfg.task.stack_size = 8192;              // Increased stack for reconnection logic
    mqtt_cfg.network.timeout_ms = 20000;          // 20s - give broker enough time for CONNACK
    mqtt_cfg.network.reconnect_timeout_ms = 3000; // 3s reconnect retry
    
    mqtt_cfg.buffer.size = 4096;                  // Optimized buffer (8192 might be overkill)
    mqtt_cfg.buffer.out_size = 2048;              // Outgoing buffer
    
    ESP_LOGI(TAG, "Unique Client ID: %s", static_client_id);
    ESP_LOGI(TAG, "MQTT connecting to %s", BROKER_URI);
    ESP_LOGI(TAG, "MQTT Configured: Priority=%d, Buffer=%d, Keepalive=%d", 
             mqtt_cfg.task.priority, mqtt_cfg.buffer.size, mqtt_cfg.session.keepalive);

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

void mqtt_publish_sensor_data(void) {
    if (mqtt_client == NULL || !is_mqtt_connected || currentState != RUNNING) return;

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "temperature", dataKandang.temp);
        cJSON_AddNumberToObject(root, "humidity", dataKandang.hum);
        cJSON_AddStringToObject(root, "timestamp", dataKandang.date_str);
        cJSON_AddBoolToObject(root, "anomaly", dataKandang.anomaly_detected);
        // PICO & AI Metrics
        cJSON_AddNumberToObject(root, "mae", dataKandang.mae);
        cJSON_AddNumberToObject(root, "inference_count", dataKandang.inference_count);
        cJSON_AddNumberToObject(root, "latency_us", dataKandang.latency_us);
        cJSON_AddNumberToObject(root, "cpu_load", dataKandang.cpu_load);
        cJSON_AddNumberToObject(root, "memory_used", dataKandang.memory_used);
        
        // Learning Metrics
        cJSON_AddNumberToObject(root, "recent_count", dataKandang.recent_count);
        cJSON_AddBoolToObject(root, "is_training", dataKandang.is_training);
        
        cJSON *golden = cJSON_AddArrayToObject(root, "golden_samples");
        for (int i = 0; i < 3; i++) {
            cJSON *sample = cJSON_CreateObject();
            cJSON_AddNumberToObject(sample, "t", dataKandang.golden_temp[i]);
            cJSON_AddNumberToObject(sample, "h", dataKandang.golden_hum[i]);
            cJSON_AddItemToArray(golden, sample);
        }

        // epoch_ms: millisecond-precision Unix timestamp for MQTT latency calculation
        struct timeval tv_now;
        gettimeofday(&tv_now, NULL);
        int64_t epoch_ms = (int64_t)tv_now.tv_sec * 1000 + (tv_now.tv_usec / 1000);
        cJSON_AddNumberToObject(root, "epoch_ms", (double)epoch_ms);

        char *json_str = cJSON_PrintUnformatted(root);
        
        // Publish Data (QoS 0 for lower latency)
        esp_mqtt_client_publish(mqtt_client, TOPIC_PUBLISH, json_str, 0, 0, 0);

        // Publish Alert explicitly if anomaly is detected (QoS 1 for reliability without QoS 2 overhead)
        if (dataKandang.anomaly_detected) {
            esp_mqtt_client_publish(mqtt_client, TOPIC_ANOMALY, "{\"alert\":\"ANOMALY DETECTED\"}", 0, 1, 0);
        }

        free(json_str);
        cJSON_Delete(root);

        xSemaphoreGive(dataMutex);
    }
}
