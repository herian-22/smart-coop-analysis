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
#define MQTT_MAX_BUFFER 10  // Diganti dari 20 ke 10 untuk stabilitas (per saran user)

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



// Lightweight structure for buffering
typedef struct {
    float temp;
    float hum;
    float mae;
    bool anomaly;
    char date_str[20];
} CompactSample;

// Double Buffering Resources
static CompactSample buffer_A[MQTT_MAX_BUFFER];
static CompactSample buffer_B[MQTT_MAX_BUFFER];
static CompactSample *active_write_buf = buffer_A;
static CompactSample *active_read_buf = NULL;
static int write_idx = 0;
static int read_count = 0;

static SemaphoreHandle_t bufferMutex = NULL;
static SemaphoreHandle_t publishSync = NULL;
static char json_static_buffer[4096]; // Static buffer for heap-less cJSON

// Background task to handle JSON formatting and Network publishing
void mqtt_publish_task(void *pvParameters) {
    while (1) {
        if (xSemaphoreTake(publishSync, portMAX_DELAY) == pdTRUE) {
            if (active_read_buf == NULL || read_count == 0) continue;

            cJSON *root = cJSON_CreateObject();
            cJSON_AddNumberToObject(root, "batch_size", read_count);
            
            cJSON *samples = cJSON_AddArrayToObject(root, "samples");
            bool any_anomaly = false;

            for (int i = 0; i < read_count; i++) {
                cJSON *sample = cJSON_CreateObject();
                cJSON_AddNumberToObject(sample, "temperature", active_read_buf[i].temp);
                cJSON_AddNumberToObject(sample, "humidity", active_read_buf[i].hum);
                cJSON_AddStringToObject(sample, "timestamp", active_read_buf[i].date_str);
                cJSON_AddBoolToObject(sample, "anomaly", active_read_buf[i].anomaly);
                cJSON_AddNumberToObject(sample, "mae", active_read_buf[i].mae);
                
                if (active_read_buf[i].anomaly) any_anomaly = true;
                cJSON_AddItemToArray(samples, sample);
            }

            // Global Metadata
            struct timeval tv_now;
            gettimeofday(&tv_now, NULL);
            int64_t epoch_ms = (int64_t)tv_now.tv_sec * 1000 + (tv_now.tv_usec / 1000);
            cJSON_AddNumberToObject(root, "epoch_ms", (double)epoch_ms);

            // Print to PREALLOCATED static buffer (Standard Industri)
            if (cJSON_PrintPreallocated(root, json_static_buffer, sizeof(json_static_buffer), 0)) {
                // Publish with Selective QoS
                // QoS 1 for anomalies, QoS 0 for routine telemetry
                int qos = any_anomaly ? 1 : 0;
                esp_mqtt_client_publish(mqtt_client, TOPIC_PUBLISH, json_static_buffer, 0, qos, 0);

                if (any_anomaly) {
                    esp_mqtt_client_publish(mqtt_client, TOPIC_ANOMALY, "{\"alert\":\"CRITICAL_ANOMALY\"}", 0, 1, 0);
                }
            }

            cJSON_Delete(root);
            
            // Release read buffer
            xSemaphoreTake(bufferMutex, portMAX_DELAY);
            active_read_buf = NULL;
            read_count = 0;
            xSemaphoreGive(bufferMutex);
        }
    }
}

void mqtt_init(void) {
    // ... structures for semaphores
    if (bufferMutex == NULL) bufferMutex = xSemaphoreCreateMutex();
    if (publishSync == NULL) publishSync = xSemaphoreCreateBinary();

    // Optimized MQTT Config
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = BROKER_URI;
    mqtt_cfg.credentials.username = "zyramqtt";
    mqtt_cfg.credentials.authentication.password = "Jefri@27";
    
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    static char static_client_id[32]; 
    snprintf(static_client_id, sizeof(static_client_id), "SmartCoop%02X%02X%02X", mac[3], mac[4], mac[5]);
    mqtt_cfg.credentials.client_id = static_client_id;
    
    mqtt_cfg.session.keepalive = 60;
    mqtt_cfg.task.priority = 6;
    mqtt_cfg.task.stack_size = 8192;
    mqtt_cfg.buffer.size = 4096;
    
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    // Start the dedicated MQTT Publish Task on Core 0 (PRO_CPU)
    xTaskCreatePinnedToCore(mqtt_publish_task, "MQTTPub", 8192, NULL, 5, NULL, 0);
}

void mqtt_publish_sensor_data(void) {
    if (mqtt_client == NULL || !is_mqtt_connected || currentState != RUNNING) return;

    SensorData localData;
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        localData = dataKandang;
        xSemaphoreGive(dataMutex);
    } else {
        return; 
    }

    // 1. Push to active write buffer (FAST)
    xSemaphoreTake(bufferMutex, portMAX_DELAY);
    if (write_idx < MQTT_MAX_BUFFER) {
        active_write_buf[write_idx].temp = localData.temp;
        active_write_buf[write_idx].hum = localData.hum;
        active_write_buf[write_idx].mae = localData.mae;
        active_write_buf[write_idx].anomaly = localData.anomaly_detected;
        strncpy(active_write_buf[write_idx].date_str, localData.date_str, 20);
        write_idx++;
    }

    // 2. Trigger swap if full or anomaly
    if (write_idx >= MQTT_MAX_BUFFER || localData.anomaly_detected) {
        if (active_read_buf == NULL) { // Last publish finished
            active_read_buf = active_write_buf;
            read_count = write_idx;
            
            // Swap write buffer
            active_write_buf = (active_write_buf == buffer_A) ? buffer_B : buffer_A;
            write_idx = 0;
            
            xSemaphoreGive(publishSync);
        } else {
            ESP_LOGW(TAG, "Buffer overflow: Publish task is too slow!");
        }
    }
    xSemaphoreGive(bufferMutex);
}
