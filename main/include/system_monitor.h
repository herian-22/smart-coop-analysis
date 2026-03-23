#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * @brief Initialize system monitoring resources.
 */
esp_err_t system_monitor_init(void);

/**
 * @brief Background task to log system health (RAM, CPU, Flash, Sensors).
 * Should be pinned to Core 0 (PRO_CPU).
 */
void taskSystemMonitor(void *pvParameters);

#ifdef __cplusplus
}
#endif
