#pragma once

#include "esp_err.h"
#include "esp_wifi.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// WiFi event types
typedef enum {
    LUX_WIFI_EVENT_STA_CONNECTED,
    LUX_WIFI_EVENT_STA_DISCONNECTED,
    LUX_WIFI_EVENT_STA_GOT_IP,
    LUX_WIFI_EVENT_AP_STARTED,
    LUX_WIFI_EVENT_AP_STACONNECTED,
    LUX_WIFI_EVENT_AP_STADISCONNECTED,
    LUX_WIFI_EVENT_SOFTAP_FALLBACK
} lux_wifi_event_t;

// WiFi event callback
typedef void (*lux_wifi_cb_t)(lux_wifi_event_t event, void* data);

// WiFi manager interface
esp_err_t wifi_manager_init(lux_wifi_cb_t cb);
esp_err_t wifi_sta_connect(const char* ssid, const char* password);
esp_err_t wifi_start_softap(const char* ssid, const char* password);
esp_err_t wifi_stop_softap(void);
bool wifi_is_connected(void);
esp_err_t wifi_get_ip_info(char* ip, char* netmask, char* gateway);

#ifdef __cplusplus
}
#endif
