#include "system_monitor.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#include "esp_sleep.h"
#include "esp_rom_sys.h"
#include "soc/soc_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SYS_MON";

esp_err_t system_monitor_init(void) {
    ESP_LOGI(TAG, "Initializing System Monitoring Module...");
    return ESP_OK;
}

void taskSystemMonitor(void *pvParameters) {
    ESP_LOGI(TAG, "System Monitor Task Started (Core %d)", xPortGetCoreID());
    
    for (;;) {
        // --- 1. MEMORY TRACKING ---
        size_t free_heap = esp_get_free_heap_size();
        size_t min_free = esp_get_minimum_free_heap_size();
        size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        
        ESP_LOGI(TAG, "[RAM] Free: %u B | MinFree: %u B | LargestBlock: %u B", 
                 (unsigned int)free_heap, (unsigned int)min_free, (unsigned int)largest_block);
        
        // --- 2. CPU & CLOCK ---
        // esp_rom_get_cpu_ticks_per_us() provides the current CPU frequency in MHz.
        uint32_t cpu_freq_mhz = esp_rom_get_cpu_ticks_per_us();
        ESP_LOGI(TAG, "[CPU] Clock: %lu MHz | Core: %d", (unsigned long)cpu_freq_mhz, xPortGetCoreID());

        // --- 3. STORAGE (NVS) ---
        nvs_stats_t nvs_stats;
        if (nvs_get_stats(NULL, &nvs_stats) == ESP_OK) {
            ESP_LOGI(TAG, "[NVS] Used: %d | Free: %d | Total: %d entries", 
                     nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
        }

        // --- 4. HARDWARE SENSORS & STATUS ---
        esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
        ESP_LOGI(TAG, "[BOOT] Wakeup Cause: %d", wakeup_cause);

        // --- 5. STACK WATERMARKS (Audit) ---
        // Note: Task handles would be better, but we can query by name if they are standard.
        // For current task:
        ESP_LOGI(TAG, "[STACK] %s Watermark: %u bytes", pcTaskGetName(NULL), uxTaskGetStackHighWaterMark(NULL));

        // --- 6. PARTITION TABLE SCAN (Condensed) ---
        esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
        if (it != NULL) {
            const esp_partition_t *p = esp_partition_get(it);
            ESP_LOGD(TAG, "[ROM] App Partition: %s | Size: %u KB", p->label, (unsigned int)p->size / 1024);
            esp_partition_iterator_release(it);
        }

        ESP_LOGI(TAG, "-------------------------------------------");
        
        vTaskDelay(pdMS_TO_TICKS(10000)); // Log every 10 seconds
    }
}
