#include "web_server.h"
#include "app_config.h"

#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "cJSON.h"

static const char *TAG = "Web_Server";

const char index_html[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Konfigurasi Smart Coop</title>
    <style>
        :root { --primary-color: #007bff; --success-color: #28a745; --light-gray: #f0f0f0; --dark-gray: #333; --white: #ffffff; --danger-color: #dc3545; }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { font-family: sans-serif; background-color: var(--light-gray); color: var(--dark-gray); display: flex; justify-content: center; align-items: center; min-height: 100vh; padding: 1rem; }
        .container { width: 100%; max-width: 450px; background-color: var(--white); padding: 2rem; border-radius: 8px; box-shadow: 0 4px 15px rgba(0,0,0,0.1); }
        h1, h2 { text-align: center; color: var(--primary-color); }
        .section { margin-bottom: 2rem; border-bottom: 1px solid #eee; padding-bottom: 2rem; }
        label { display: block; margin-bottom: 0.5rem; font-weight: bold; }
        input { width: 100%; padding: 0.75rem; border: 1px solid #ccc; border-radius: 4px; margin-bottom: 1rem; }
        button { width: 100%; padding: 0.8rem; border: none; border-radius: 4px; background-color: var(--primary-color); color: var(--white); font-weight: bold; cursor: pointer; }
        .wifi-item { padding: 0.75rem; cursor: pointer; border-bottom: 1px solid #eee; display: flex; justify-content: space-between; }
        .loader { border: 4px solid #f3f3f3; border-top: 4px solid var(--primary-color); border-radius: 50%; width: 24px; height: 24px; animation: spin 1s linear infinite; margin: 1rem auto; display: none; }
        @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
    </style>
</head>
<body>
    <div class="container">
        <h1>Smart Coop</h1>
        <div class="section">
            <h2>WiFi Setup</h2>
            <button id="scanButton">Pindai WiFi</button>
            <div id="loader" class="loader"></div>
            <div id="wifiList"></div>
            <form id="configForm" style="display: none; margin-top:10px;">
                <input type="text" id="ssid" name="ssid" readonly>
                <input type="password" id="password" name="password" placeholder="Password WiFi">
                <input type="text" id="coopId" name="coopId" placeholder="ID Kandang">
                <button type="submit">Simpan & Hubungkan</button>
            </form>
            <div id="message" style="margin-top:10px; text-align:center;"></div>
        </div>
        <div class="section">
            <h2>OTA Update</h2>
            <form id="upload_form">
                <input type="file" id="file" accept=".bin">
                <button type="submit" style="background-color: var(--success-color);">Update Firmware</button>
            </form>
            <div id="progress" style="margin-top:10px; text-align:center;"></div>
        </div>
    </div>
    <script>
        const scanBtn = document.getElementById('scanButton');
        const wifiList = document.getElementById('wifiList');
        const configForm = document.getElementById('configForm');
        const message = document.getElementById('message');

        scanBtn.onclick = async () => {
            document.getElementById('loader').style.display = 'block';
            wifiList.innerHTML = '';
            try {
                const res = await fetch('/scan');
                const data = await res.json();
                data.forEach(net => {
                    const div = document.createElement('div');
                    div.className = 'wifi-item';
                    div.innerHTML = `<span>${net.ssid}</span><span>${net.rssi} dBm</span>`;
                    div.onclick = () => { document.getElementById('ssid').value = net.ssid; configForm.style.display = 'block'; };
                    wifiList.appendChild(div);
                });
            } catch (e) { message.innerText = 'Gagal memindai'; }
            document.getElementById('loader').style.display = 'none';
        };

        configForm.onsubmit = async (e) => {
            e.preventDefault();
            const body = JSON.stringify(Object.fromEntries(new FormData(configForm)));
            message.innerText = 'Menyimpan...';
            const res = await fetch('/save', { method: 'POST', body });
            if (res.ok) message.innerText = 'Berhasil! Merestart...';
        };
    </script>
</body>
</html>
)rawliteral";

static esp_err_t index_get_handler(httpd_req_t *req) {
    httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t scan_get_handler(httpd_req_t *req) {
    uint16_t ap_count = 0;
    wifi_scan_config_t scan_config = {};
    scan_config.ssid = NULL;
    scan_config.show_hidden = false;
    esp_wifi_scan_start(&scan_config, true);
    esp_wifi_scan_get_ap_num(&ap_count);
    wifi_ap_record_t *ap_info = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
    esp_wifi_scan_get_ap_records(&ap_count, ap_info);

    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < ap_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", (char *)ap_info[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", ap_info[i].rssi);
        cJSON_AddItemToArray(root, item);
    }
    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    
    free(json_str); 
    cJSON_Delete(root); 
    free(ap_info);
    
    return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, req->content_len);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';
    cJSON *root = cJSON_Parse(buf);
    
    cJSON *ssid_item = cJSON_GetObjectItem(root, "ssid");
    cJSON *pass_item = cJSON_GetObjectItem(root, "password");
    
    if (ssid_item && pass_item) {
        wifi_config_t wifi_config = {};
        strncpy((char *)wifi_config.sta.ssid, ssid_item->valuestring, sizeof(wifi_config.sta.ssid));
        strncpy((char *)wifi_config.sta.password, pass_item->valuestring, sizeof(wifi_config.sta.password));
        
        esp_wifi_set_storage(WIFI_STORAGE_FLASH);
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        
        httpd_resp_sendstr(req, "OK");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    } else {
        httpd_resp_send_500(req);
    }
    
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t update_post_handler(httpd_req_t *req) {
    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    char *buf = (char *)malloc(1024);
    int data_read;
    while ((data_read = httpd_req_recv(req, buf, 1024)) > 0) {
        esp_ota_write(update_handle, buf, data_read);
    }
    esp_ota_end(update_handle);
    esp_ota_set_boot_partition(update_partition);
    free(buf);
    
    httpd_resp_sendstr(req, "Update Sukses, Restarting...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return ESP_OK;
}

esp_err_t web_server_start(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_idx = { .uri = "/", .method = HTTP_GET, .handler = index_get_handler, .user_ctx = NULL };
        httpd_uri_t uri_scn = { .uri = "/scan", .method = HTTP_GET, .handler = scan_get_handler, .user_ctx = NULL };
        httpd_uri_t uri_sav = { .uri = "/save", .method = HTTP_POST, .handler = save_post_handler, .user_ctx = NULL };
        httpd_uri_t uri_upd = { .uri = "/update", .method = HTTP_POST, .handler = update_post_handler, .user_ctx = NULL };
        
        httpd_register_uri_handler(server, &uri_idx);
        httpd_register_uri_handler(server, &uri_scn);
        httpd_register_uri_handler(server, &uri_sav);
        httpd_register_uri_handler(server, &uri_upd);
        
        ESP_LOGI(TAG, "Web Server started");
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "Failed to start Web Server");
    return ESP_FAIL;
}
