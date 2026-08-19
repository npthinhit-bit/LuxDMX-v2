#include "wifi_events.h"
#include "logger.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"

static const char* TAG = "wifi_events";

static wifi_event_cb_t g_event_cb = NULL;

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (g_event_cb) {
        g_event_cb(event_id, event_data);
    }
}

esp_err_t wifi_events_init(wifi_event_cb_t cb) {
    g_event_cb = cb;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    LOG_INFO(TAG, "WiFi events initialized");
    return ESP_OK;
}
