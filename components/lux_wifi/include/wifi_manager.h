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

// Network state (spec 32)
typedef enum {
    NET_STATE_INIT,        // Just initialized
    NET_STATE_CONNECTING,  // Attempting STA connection
    NET_STATE_STATION,     // Connected as station
    NET_STATE_AP_ONLY,     // SoftAP active (no station)
    NET_STATE_AP_AND_STA,  // Both AP and station active
    NET_STATE_DISCONNECTED // Station disconnected, will retry
} net_state_t;

// WiFi manager interface
esp_err_t wifi_manager_init(lux_wifi_cb_t cb);
esp_err_t wifi_sta_connect(const char* ssid, const char* password);
esp_err_t wifi_sta_connect_from_config(void);
esp_err_t wifi_start_softap(const char* ssid, const char* password);
esp_err_t wifi_stop_softap(void);
bool wifi_is_connected(void);
esp_err_t wifi_get_ip_info(char* ip, char* netmask, char* gateway);
net_state_t wifi_get_net_state(void);
bool wifi_should_enter_portal(void);

#ifdef UNIT_TEST
int wifi_backoff_ms_test(int retry);
void wifi_manager_reset_state_test(void);
#endif

#ifdef __cplusplus
}
#endif
