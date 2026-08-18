#pragma once

#include "esp_err.h"
#include "esp_wifi.h"

// WiFi event types
typedef enum {
    WIFI_EVENT_STA_CONNECTED,
    WIFI_EVENT_STA_DISCONNECTED,
    WIFI_EVENT_STA_GOT_IP,
    WIFI_EVENT_AP_STARTED,
    WIFI_EVENT_AP_STACONNECTED,
    WIFI_EVENT_AP_STADISCONNECTED,
    WIFI_EVENT_SOFTAP_FALLBACK
} wifi_event_t;

// WiFi event callback
typedef void (*wifi_event_cb_t)(wifi_event_t event, void* data);

// WiFi manager interface
esp_err_t wifi_manager_init(wifi_event_cb_t cb);
esp_err_t wifi_sta_connect(const char* ssid, const char* password);
esp_err_t wifi_start_softap(const char* ssid, const char* password);
esp_err_t wifi_stop_softap(void);
bool wifi_is_connected(void);
esp_err_t wifi_get_ip_info(char* ip, char* netmask, char* gateway);