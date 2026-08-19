#include "web_server.h"
#include "web_routes.h"
#include "common.h"
#include "logger.h"
#include "config_engine.h"
#include "config_json.h"
#include "web_frontend.h"
#include "web_assets.h"
#include "wifi_manager.h"
#include "wifi_config.h"
#include "captive_portal.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include <string.h>

static const char* TAG = "web_routes";

// Get device information as JSON
static esp_err_t info_get_handler(httpd_req_t* req) {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return httpd_resp_send_500(req);
    }

    // Device info
    cJSON_AddStringToObject(root, "device", "LuxDMX");
    cJSON_AddStringToObject(root, "firmware", "2.0.0");
    cJSON_AddStringToObject(root, "board", "esp32s3_n16r8"); // TODO: Get from hw_get_board_type()

    // WiFi info
    char ip[16], netmask[16], gateway[16];
    wifi_get_ip_info(ip, netmask, gateway);
    cJSON_AddStringToObject(root, "ip", ip);
    cJSON_AddStringToObject(root, "netmask", netmask);
    cJSON_AddStringToObject(root, "gateway", gateway);

    // Get SSID
    wifi_config_t sta_config;
    if (esp_wifi_get_config(WIFI_IF_STA, &sta_config) == ESP_OK) {
        cJSON_AddStringToObject(root, "ssid", (char*)sta_config.sta.ssid);
    }

    int rssi = 0;
    if (wifi_is_connected()) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            rssi = ap_info.rssi;
        }
    }
    cJSON_AddNumberToObject(root, "rssi", rssi);

    // Heap info
    cJSON_AddNumberToObject(root, "heap_free", esp_get_free_heap_size());

    // Configuration
    cJSON* config_obj;
    config_export_json(&config_obj);
    cJSON_AddItemToObject(root, "config", config_obj);

    char* json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_string) {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    return err;
}

// Serve index page
static esp_err_t index_get_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    const char* html = web_frontend_get_index();
    return httpd_resp_send(req, html, strlen(html));
}

// Serve config page
static esp_err_t config_get_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    const char* html = web_frontend_get_config();
    return httpd_resp_send(req, html, strlen(html));
}

// Handle config POST
static esp_err_t config_post_handler(httpd_req_t* req) {
    int total_len = req->content_len;
    if (total_len <= 0 || total_len > 8192) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }

    char* buf = malloc(total_len + 1);
    if (!buf) {
        return httpd_resp_send_500(req);
    }

    int received = httpd_req_recv(req, buf, total_len);
    if (received <= 0) {
        free(buf);
        return httpd_resp_send_500(req);
    }
    buf[received] = '\0';

    // Parse form data
    cJSON* root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    esp_err_t err = config_import_json(root);
    cJSON_Delete(root);

    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid configuration");
        return ESP_FAIL;
    }

    // Save to NVS
    err = config_save();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save configuration");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"status\":\"ok\"}", 14);
}

// Serve WiFi scan results
static esp_err_t wifi_scan_get_handler(httpd_req_t* req) {
    // Trigger scan
    esp_wifi_scan_start(NULL, true);

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    wifi_ap_record_t* ap_records = malloc(ap_count * sizeof(wifi_ap_record_t));
    if (!ap_records) {
        return httpd_resp_send_500(req);
    }

    esp_wifi_scan_get_ap_records(&ap_count, ap_records);

    cJSON* root = cJSON_CreateArray();
    for (uint16_t i = 0; i < ap_count; i++) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", (char*)ap_records[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", ap_records[i].rssi);
        cJSON_AddNumberToObject(item, "authmode", ap_records[i].authmode);
        cJSON_AddItemToArray(root, item);
    }

    free(ap_records);

    char* json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_string) {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, json_string, strlen(json_string));
    free(json_string);
    return err;
}

// Serve setup page
static esp_err_t setup_get_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    const char* html = web_frontend_get_setup();
    return httpd_resp_send(req, html, strlen(html));
}

// Handle setup POST (save WiFi credentials)
static esp_err_t setup_post_handler(httpd_req_t* req) {
    int total_len = req->content_len;
    if (total_len <= 0 || total_len > 2048) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }

    char* buf = malloc(total_len + 1);
    if (!buf) {
        return httpd_resp_send_500(req);
    }

    int received = httpd_req_recv(req, buf, total_len);
    if (received <= 0) {
        free(buf);
        return httpd_resp_send_500(req);
    }
    buf[received] = '\0';

    // Parse form data (ssid=xxx&psk=yyy)
    char ssid[64] = {0};
    char psk[64] = {0};

    char* ssid_start = strstr(buf, "ssid=");
    char* psk_start = strstr(buf, "psk=");

    if (ssid_start) {
        ssid_start += 5; // skip "ssid="
        char* end = strchr(ssid_start, '&');
        if (end) {
            size_t len = end - ssid_start;
            if (len < sizeof(ssid)) {
                strncpy(ssid, ssid_start, len);
                ssid[len] = '\0';
            }
        } else {
            strncpy(ssid, ssid_start, sizeof(ssid) - 1);
        }
    }

    if (psk_start) {
        psk_start += 4; // skip "psk="
        char* end = strchr(psk_start, '&');
        if (end) {
            size_t len = end - psk_start;
            if (len < sizeof(psk)) {
                strncpy(psk, psk_start, len);
                psk[len] = '\0';
            }
        } else {
            strncpy(psk, psk_start, sizeof(psk) - 1);
        }
    }

    free(buf);

    if (strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }

    // Save WiFi credentials to NVS
    esp_err_t err = wifi_config_save(ssid, psk);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save WiFi config");
        return ESP_FAIL;
    }

    // Also update config engine
    config_set_value("wifi_ssid", ssid);
    config_set_value("wifi_password", psk);
    config_save();

    // Stop captive portal
    captive_portal_stop();

    // Respond and reboot
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\",\"message\":\"WiFi saved, rebooting...\"}", 50);

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

// Serve static assets
static esp_err_t assets_get_handler(httpd_req_t* req) {
    const char* uri = req->uri;

    if (strstr(uri, ".css")) {
        httpd_resp_set_type(req, "text/css");
    } else if (strstr(uri, ".js")) {
        httpd_resp_set_type(req, "application/javascript");
    } else if (strstr(uri, ".ico")) {
        httpd_resp_set_type(req, "image/x-icon");
    } else if (strstr(uri, ".png")) {
        httpd_resp_set_type(req, "image/png");
    }

    const char* content = web_assets_get(uri);
    if (!content) {
        return httpd_resp_send_404(req);
    }

    size_t len = strlen(content);
    return httpd_resp_send(req, content, len);
}

// Register all routes
esp_err_t web_routes_register(httpd_handle_t server) {
    static const httpd_uri_t setup_get_uri = {
        .uri = "/setup",
        .method = HTTP_GET,
        .handler = setup_get_handler,
        .user_ctx = NULL
    };

    static const httpd_uri_t setup_post_uri = {
        .uri = "/setup",
        .method = HTTP_POST,
        .handler = setup_post_handler,
        .user_ctx = NULL
    };

    static const httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_get_handler,
        .user_ctx = NULL
    };

    static const httpd_uri_t config_get_uri = {
        .uri = "/config",
        .method = HTTP_GET,
        .handler = config_get_handler,
        .user_ctx = NULL
    };

    static const httpd_uri_t config_post_uri = {
        .uri = "/config",
        .method = HTTP_POST,
        .handler = config_post_handler,
        .user_ctx = NULL
    };

    static const httpd_uri_t info_uri = {
        .uri = "/info.json",
        .method = HTTP_GET,
        .handler = info_get_handler,
        .user_ctx = NULL
    };

    static const httpd_uri_t wifi_scan_uri = {
        .uri = "/wifi/scan",
        .method = HTTP_GET,
        .handler = wifi_scan_get_handler,
        .user_ctx = NULL
    };

    static const httpd_uri_t assets_uri = {
        .uri = "/assets/*",
        .method = HTTP_GET,
        .handler = assets_get_handler,
        .user_ctx = NULL
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &setup_get_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &setup_post_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &index_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &config_get_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &config_post_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &info_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &wifi_scan_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &assets_uri));

    return ESP_OK;
}