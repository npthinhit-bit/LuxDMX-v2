#include "wifi_manager.h"
#include "common.h"
#include "logger.h"
#include "wifi_events.h"
#include "wifi_config.h"
#include "captive_portal.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char* TAG = "wifi_manager";

// WiFi state
typedef enum {
    WIFI_STATE_IDLE,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_AP_ACTIVE,
    WIFI_STATE_ERROR
} wifi_state_t;

// WiFi manager context
typedef struct {
    wifi_state_t state;
    lux_wifi_cb_t event_callback;
    SemaphoreHandle_t mutex;
    esp_netif_t* sta_netif;
    esp_netif_t* ap_netif;
    int retry_count;
    bool softap_active;
} wifi_manager_ctx_t;

static wifi_manager_ctx_t ctx = {
    .state = WIFI_STATE_IDLE,
    .event_callback = NULL,
    .mutex = NULL,
    .sta_netif = NULL,
    .ap_netif = NULL,
    .retry_count = 0,
    .softap_active = false
};

// Default WiFi configuration
#define DEFAULT_SOFTAP_SSID "LuxDMX-Setup"
#define DEFAULT_SOFTAP_PASSWORD "dmxpassword"
#define MAX_RETRY_ATTEMPTS 10
#define RETRY_DELAY_MS 5000

// WiFi event handler (ESP-IDF level)
static void wifi_event_handler(int32_t event_id, void* data) {
    switch (event_id) {
        case WIFI_EVENT_STA_CONNECTED:
            ctx.state = WIFI_STATE_CONNECTED;
            ctx.retry_count = 0;
            LOG_INFO(TAG, "WiFi station connected");
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*)data;

            // Check if we should retry
            if (ctx.retry_count < MAX_RETRY_ATTEMPTS) {
                ctx.state = WIFI_STATE_CONNECTING;
                ctx.retry_count++;
                LOG_INFO(TAG, "WiFi disconnected, retry %d/%d (reason: %d)",
                        ctx.retry_count, MAX_RETRY_ATTEMPTS, disconnected->reason);

                // Schedule retry
                vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
                esp_wifi_connect();
            } else {
                ctx.state = WIFI_STATE_DISCONNECTED;
                LOG_INFO(TAG, "Max retry attempts reached, starting SoftAP fallback");

                // Start SoftAP fallback
                if (!ctx.softap_active) {
                    wifi_start_softap(DEFAULT_SOFTAP_SSID, DEFAULT_SOFTAP_PASSWORD);
                    if (ctx.event_callback) {
                        ctx.event_callback(LUX_WIFI_EVENT_SOFTAP_FALLBACK, NULL);
                    }
                }
            }
            break;
        }

        case IP_EVENT_STA_GOT_IP: {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*)data;
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));
            LOG_INFO(TAG, "Got IP: %s", ip_str);
            break;
        }

        case WIFI_EVENT_AP_START:
            ctx.state = WIFI_STATE_AP_ACTIVE;
            ctx.softap_active = true;
            LOG_INFO(TAG, "SoftAP started");

            // Start captive portal
            captive_portal_start();
            break;

        case WIFI_EVENT_AP_STACONNECTED:
            LOG_INFO(TAG, "Station connected to SoftAP");
            break;

        case WIFI_EVENT_AP_STADISCONNECTED:
            LOG_INFO(TAG, "Station disconnected from SoftAP");
            break;

        default:
            break;
    }
}

// Initialize WiFi manager
esp_err_t wifi_manager_init(lux_wifi_cb_t cb) {
    // Create mutex
    ctx.mutex = xSemaphoreCreateMutex();
    if (ctx.mutex == NULL) {
        LOG_ERROR(TAG, "Failed to create WiFi manager mutex");
        return ESP_ERR_NO_MEM;
    }

    // Store event callback
    ctx.event_callback = cb;

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default WiFi station interface
    ctx.sta_netif = esp_netif_create_default_wifi_sta();
    if (ctx.sta_netif == NULL) {
        LOG_ERROR(TAG, "Failed to create WiFi station interface");
        return ESP_ERR_NO_MEM;
    }

    // Create default WiFi AP interface
    ctx.ap_netif = esp_netif_create_default_wifi_ap();
    if (ctx.ap_netif == NULL) {
        LOG_ERROR(TAG, "Failed to create WiFi AP interface");
        return ESP_ERR_NO_MEM;
    }

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Initialize WiFi events
    ESP_ERROR_CHECK(wifi_events_init(wifi_event_handler));
    // Set WiFi mode to station + AP
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // Configure SoftAP
    wifi_config_t ap_config = {
        .ap = {
            .ssid = DEFAULT_SOFTAP_SSID,
            .ssid_len = strlen(DEFAULT_SOFTAP_SSID),
            .password = DEFAULT_SOFTAP_PASSWORD,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        }
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    LOG_INFO(TAG, "WiFi manager initialized");
    return ESP_OK;
}

// Connect to WiFi station
esp_err_t wifi_sta_connect(const char* ssid, const char* password) {
    if (xSemaphoreTake(ctx.mutex, portMAX_DELAY) != pdTRUE) {
        LOG_ERROR(TAG, "Failed to acquire WiFi manager mutex");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = ESP_OK;

    // Save configuration
    err = wifi_config_save(ssid, password);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to save WiFi configuration");
        xSemaphoreGive(ctx.mutex);
        return err;
    }

    // Configure station
    wifi_config_t sta_config = {0};
    strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char*)sta_config.sta.password, password, sizeof(sta_config.sta.password) - 1);

    // Set WiFi configuration
    err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to set WiFi configuration: %s", esp_err_to_name(err));
        xSemaphoreGive(ctx.mutex);
        return err;
    }

    // Start connection
    ctx.state = WIFI_STATE_CONNECTING;
    ctx.retry_count = 0;
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to start WiFi connection: %s", esp_err_to_name(err));
        xSemaphoreGive(ctx.mutex);
        return err;
    }

    LOG_INFO(TAG, "Connecting to WiFi: %s", ssid);
    xSemaphoreGive(ctx.mutex);
    return ESP_OK;
}

// Start SoftAP
esp_err_t wifi_start_softap(const char* ssid, const char* password) {
    if (xSemaphoreTake(ctx.mutex, portMAX_DELAY) != pdTRUE) {
        LOG_ERROR(TAG, "Failed to acquire WiFi manager mutex");
        return ESP_ERR_TIMEOUT;
    }

    // Configure SoftAP
    wifi_config_t ap_config = {0};
    strncpy((char*)ap_config.ap.ssid, ssid, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(ssid);

    if (password && strlen(password) > 0) {
        strncpy((char*)ap_config.ap.password, password, sizeof(ap_config.ap.password) - 1);
        ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    } else {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ap_config.ap.max_connection = 4;
    ap_config.ap.channel = 6;

    // Set SoftAP configuration
    esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to set SoftAP configuration: %s", esp_err_to_name(err));
        xSemaphoreGive(ctx.mutex);
        return err;
    }

    // If SoftAP is not active, start it
    if (!ctx.softap_active) {
        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (err != ESP_OK) {
            LOG_ERROR(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(err));
            xSemaphoreGive(ctx.mutex);
            return err;
        }
    }

    LOG_INFO(TAG, "SoftAP started: %s", ssid);
    xSemaphoreGive(ctx.mutex);
    return ESP_OK;
}

// Stop SoftAP
esp_err_t wifi_stop_softap(void) {
    if (xSemaphoreTake(ctx.mutex, portMAX_DELAY) != pdTRUE) {
        LOG_ERROR(TAG, "Failed to acquire WiFi manager mutex");
        return ESP_ERR_TIMEOUT;
    }

    // Stop captive portal
    captive_portal_stop();

    // Set WiFi mode to station only
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(err));
        xSemaphoreGive(ctx.mutex);
        return err;
    }

    ctx.softap_active = false;
    LOG_INFO(TAG, "SoftAP stopped");
    xSemaphoreGive(ctx.mutex);
    return ESP_OK;
}

// Check if WiFi is connected
bool wifi_is_connected(void) {
    return ctx.state == WIFI_STATE_CONNECTED;
}

// Get IP information
esp_err_t wifi_get_ip_info(char* ip, char* netmask, char* gateway) {
    if (ctx.state != WIFI_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t err = esp_netif_get_ip_info(ctx.sta_netif, &ip_info);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to get IP information: %s", esp_err_to_name(err));
        return err;
    }

    if (ip) {
        snprintf(ip, 16, IPSTR, IP2STR(&ip_info.ip));
    }
    if (netmask) {
        snprintf(netmask, 16, IPSTR, IP2STR(&ip_info.netmask));
    }
    if (gateway) {
        snprintf(gateway, 16, IPSTR, IP2STR(&ip_info.gw));
    }

    return ESP_OK;
}