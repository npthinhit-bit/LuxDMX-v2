#include "wifi_manager.h"
#include "common.h"
#include "logger.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"

static const char* TAG = "wifi_events";

// WiFi event callback
static wifi_event_cb_t wifi_event_callback = NULL;

// Event handler for WiFi events
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                LOG_DEBUG(TAG, "WiFi station started");
                break;

            case WIFI_EVENT_STA_CONNECTED:
                LOG_INFO(TAG, "WiFi station connected to AP");
                if (wifi_event_callback) {
                    wifi_event_callback(WIFI_EVENT_STA_CONNECTED, NULL);
                }
                break;

            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*)event_data;
                LOG_INFO(TAG, "WiFi station disconnected (reason: %d)", disconnected->reason);
                if (wifi_event_callback) {
                    wifi_event_callback(WIFI_EVENT_STA_DISCONNECTED, disconnected);
                }
                break;
            }

            case WIFI_EVENT_AP_START:
                LOG_INFO(TAG, "WiFi SoftAP started");
                if (wifi_event_callback) {
                    wifi_event_callback(WIFI_EVENT_AP_STARTED, NULL);
                }
                break;

            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t* connected = (wifi_event_ap_staconnected_t*)event_data;
                char mac_str[18];
                snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                        connected->mac[0], connected->mac[1], connected->mac[2],
                        connected->mac[3], connected->mac[4], connected->mac[5]);
                LOG_INFO(TAG, "Station connected to SoftAP: MAC: %s", mac_str);
                if (wifi_event_callback) {
                    wifi_event_callback(WIFI_EVENT_AP_STACONNECTED, connected);
                }
                break;
            }

            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t* disconnected = (wifi_event_ap_stadisconnected_t*)event_data;
                char mac_str[18];
                snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                        disconnected->mac[0], disconnected->mac[1], disconnected->mac[2],
                        disconnected->mac[3], disconnected->mac[4], disconnected->mac[5]);
                LOG_INFO(TAG, "Station disconnected from SoftAP: MAC: %s", mac_str);
                if (wifi_event_callback) {
                    wifi_event_callback(WIFI_EVENT_AP_STADISCONNECTED, disconnected);
                }
                break;
            }

            default:
                LOG_DEBUG(TAG, "Unhandled WiFi event: %d", event_id);
                break;
        }
    }
    else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP: {
                ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
                char ip_str[16];
                snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));
                LOG_INFO(TAG, "Got IP: %s", ip_str);
                if (wifi_event_callback) {
                    wifi_event_callback(WIFI_EVENT_STA_GOT_IP, event);
                }
                break;
            }
        }
    }
}

// Initialize WiFi event handling
esp_err_t wifi_events_init(wifi_event_cb_t cb) {
    // Store the callback
    wifi_event_callback = cb;

    // Initialize event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Register WiFi event handler
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    // Register IP event handler
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    return ESP_OK;
}