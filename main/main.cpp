#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hw.h"
#include "boards.h"
#include "wifi_manager.h"
#include "led_driver.h"
#include "config_engine.h"
#include "config_schema.h"
#include "config_serial.h"
#include "dmx_buffer.h"
#include "dmx_rmt_tx.h"
#include "web_server.h"
#include "logger.h"
#include "captive_portal.h"
#include "tasks.h"
#include "wifi_config.h"

#include <stdlib.h>

static const char* TAG = "main";

// WiFi event handler
static void wifi_event_handler(lux_wifi_event_t event, void* data) {
    switch (event) {
        case LUX_WIFI_EVENT_STA_CONNECTED:
            LOG_INFO(TAG, "WiFi connected to AP");
            break;
        case LUX_WIFI_EVENT_STA_DISCONNECTED:
            LOG_INFO(TAG, "WiFi disconnected from AP");
            break;
        case LUX_WIFI_EVENT_STA_GOT_IP: {
            char ip[16], netmask[16], gateway[16];
            wifi_get_ip_info(ip, netmask, gateway);
            LOG_INFO(TAG, "Got IP: %s", ip);
            break;
        }
        case LUX_WIFI_EVENT_AP_STARTED:
            LOG_INFO(TAG, "SoftAP started");
            break;
        case LUX_WIFI_EVENT_SOFTAP_FALLBACK:
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

    // Initialize hardware (board detection)
    ESP_ERROR_CHECK(hw_init());
    LOG_INFO(TAG, "Hardware initialized - board: %s", hw_get_board_config()->name);

    // Initialize LED driver with boot pattern
    led_driver_t* led_driver = led_driver_create();
    if (led_driver) {
        ESP_ERROR_CHECK(led_driver->init(led_driver));
        ESP_ERROR_CHECK(led_driver->set_pattern(led_driver, LED_PATTERN_BOOT));
        ESP_ERROR_CHECK(led_driver->set_brightness(led_driver, 80));
    } else {
        LOG_WARN(TAG, "Failed to create LED driver");
    }

    // Initialize configuration engine
    ESP_ERROR_CHECK(config_engine_init());
    config_load();
    LOG_INFO(TAG, "Configuration loaded");

    // Initialize DMX outputs (spec 14 §6.1, 07)
    // Crash guard (spec 35): disable outputs if recovering from a crash
    uint8_t crashCount = dmxInitGuardBegin();
    sanitizeOutputs();
    outputInitAll();
    dmxInitGuardEnd(crashCount);
    dmx_tx_task_start();
    LOG_INFO(TAG, "DMX outputs initialized");

    // Update LED pattern to connecting
    if (led_driver) {
        led_driver->set_pattern(led_driver, LED_PATTERN_WIFI_CONNECTING);
        led_driver->update(led_driver);
    }

    // Initialize WiFi manager
    ESP_ERROR_CHECK(wifi_manager_init(wifi_event_handler));

    // Determine if we should enter provisioning portal
    bool enter_portal = false;

    if (wifi_config_exists()) {
        // Try to connect with stored credentials
        LOG_INFO(TAG, "Attempting WiFi connection with stored credentials");
        ret = wifi_sta_connect_from_config();
        if (ret != ESP_OK) {
            LOG_WARN(TAG, "Failed to start WiFi connection: %s", esp_err_to_name(ret));
            enter_portal = true;
        }
    } else {
        // No credentials stored - enter provisioning portal
        LOG_INFO(TAG, "No WiFi credentials found, entering setup portal");
        enter_portal = true;
    }

    // Check for GPIO0 forced portal
    if (wifi_should_enter_portal()) {
        enter_portal = true;
    }

    if (enter_portal) {
        // Start SoftAP for provisioning (SSID = hostname from config)
        char hostname[32] = {0};
        char* stored = config_get_value("hostname");
        if (stored && strlen(stored) > 0) {
            strncpy(hostname, stored, sizeof(hostname) - 1);
        } else {
            strncpy(hostname, "luxdmx", sizeof(hostname) - 1);
        }
        free(stored);

        ESP_ERROR_CHECK(wifi_start_softap(hostname, NULL));
        LOG_INFO(TAG, "Setup portal active on SSID: %s", hostname);

        // Update LED to AP mode
        if (led_driver) {
            led_driver->set_pattern(led_driver, LED_PATTERN_AP_ACTIVE);
            led_driver->update(led_driver);
        }
    }

    // Initialize web server
    ESP_ERROR_CHECK(web_server_init());
    ESP_ERROR_CHECK(web_server_start());
    LOG_INFO(TAG, "Web server started");

    // Main application loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Update LED status
        if (led_driver) {
            // Update LED pattern based on network state
            net_state_t net_state = wifi_get_net_state();
            switch (net_state) {
                case NET_STATE_STATION:
                    led_driver->set_pattern(led_driver, LED_PATTERN_WIFI_CONNECTED);
                    break;
                case NET_STATE_AP_AND_STA:
                    // Both STA and AP active - show connected
                    led_driver->set_pattern(led_driver, LED_PATTERN_WIFI_CONNECTED);
                    break;
                case NET_STATE_CONNECTING:
                case NET_STATE_DISCONNECTED:
                    led_driver->set_pattern(led_driver, LED_PATTERN_WIFI_CONNECTING);
                    break;
                case NET_STATE_AP_ONLY:
                    led_driver->set_pattern(led_driver, LED_PATTERN_AP_ACTIVE);
                    break;
                default:
                    break;
            }
            led_driver->update(led_driver);
        }

        // Send WebSocket status updates
        web_websocket_send_status();

        // Check for serial console reboot request
        if (config_serial_check_reboot()) {
            LOG_INFO(TAG, "Reboot requested via serial console");
            esp_restart();
        }
    }
}
