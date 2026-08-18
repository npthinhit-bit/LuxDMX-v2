#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hw.h"
#include "wifi_manager.h"
#include "led_driver.h"
#include "config_engine.h"
#include "web_server.h"
#include "logger.h"

static const char* TAG = "main";

// WiFi event handler
static void wifi_event_handler(wifi_event_t event, void* data) {
    switch (event) {
        case WIFI_EVENT_STA_CONNECTED:
            LOG_INFO(TAG, "WiFi connected to AP");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            LOG_INFO(TAG, "WiFi disconnected from AP");
            break;
        case WIFI_EVENT_STA_GOT_IP: {
            char ip[16], netmask[16], gateway[16];
            wifi_get_ip_info(ip, netmask, gateway);
            LOG_INFO(TAG, "Got IP: %s", ip);
            break;
        }
        case WIFI_EVENT_AP_STARTED:
            LOG_INFO(TAG, "SoftAP started");
            break;
        case WIFI_EVENT_SOFTAP_FALLBACK:
            LOG_INFO(TAG, "Fallback to SoftAP mode");
            break;
        default:
            break;
    }
}

// Main application
extern "C" void app_main(void) {
    // Initialize logging
    logger_init();
    LOG_INFO(TAG, "LuxDMX-v2 starting...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize hardware
    ESP_ERROR_CHECK(hw_init());
    LOG_INFO(TAG, "Hardware initialized");

    // Initialize LED driver
    led_driver_t* led_driver = led_driver_create();
    ESP_ERROR_CHECK(led_driver->init());
    ESP_ERROR_CHECK(led_driver->set_pattern(LED_PATTERN_BOOT));

    // Initialize configuration
    ESP_ERROR_CHECK(config_engine_init());
    ESP_ERROR_CHECK(config_load());
    LOG_INFO(TAG, "Configuration loaded");

    // Initialize WiFi
    ESP_ERROR_CHECK(wifi_manager_init(wifi_event_handler));

    // Start WiFi connection
    // In a real implementation, this would use stored credentials
    ESP_ERROR_CHECK(wifi_sta_connect("SSID", "PASSWORD"));

    // Initialize web server
    ESP_ERROR_CHECK(web_server_init());
    ESP_ERROR_CHECK(web_server_start());
    LOG_INFO(TAG, "Web server started");

    // Main application loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Update LED status
        ESP_ERROR_CHECK(led_driver->update());

        // Send WebSocket status updates
        web_websocket_send_status();
    }
}