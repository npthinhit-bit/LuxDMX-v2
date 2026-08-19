#include "wifi_manager.h"
#include "common.h"
#include "logger.h"
#include "wifi_events.h"
#include "wifi_config.h"
#include "config_engine.h"
#include "captive_portal.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char* TAG = "wifi_manager";

// Default WiFi configuration (spec 33)
#define DEFAULT_SOFTAP_SSID "LuxDMX-Setup"
#define DEFAULT_SOFTAP_PASSWORD ""
#define MAX_RETRY_ATTEMPTS 10
#define BACKOFF_BASE_MS 2000   // 2s base
#define BACKOFF_MAX_MS  60000  // 60s max

// WiFi manager context
typedef struct {
    net_state_t state;
    lux_wifi_cb_t event_callback;
    SemaphoreHandle_t mutex;
    esp_netif_t* sta_netif;
    esp_netif_t* ap_netif;
    int retry_count;
    bool softap_active;
    bool portal_forced;       // GPIO0 held during boot
} wifi_manager_ctx_t;

static wifi_manager_ctx_t ctx = {
    .state = NET_STATE_INIT,
    .event_callback = NULL,
    .mutex = NULL,
    .sta_netif = NULL,
    .ap_netif = NULL,
    .retry_count = 0,
    .softap_active = false,
    .portal_forced = false
};

static int compute_backoff_ms(int retry) {
    // Exponential backoff: base * 2^(retry), capped at max
    int delay = BACKOFF_BASE_MS << (retry < 5 ? retry : 5);
    if (delay > BACKOFF_MAX_MS) {
        delay = BACKOFF_MAX_MS;
    }
    return delay;
}

// Check if GPIO0 is held LOW (forced setup portal, spec 33)
static void check_forced_portal(void* arg) {
    // Check GPIO0 - if LOW for 3 seconds, force portal
    // This runs once at startup before WiFi init
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_0),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Sample for 3 seconds (check every 100ms)
    int low_count = 0;
    for (int i = 0; i < 30; i++) {
        if (gpio_get_level(GPIO_NUM_0) == 0) {
            low_count++;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // If LOW was detected at least once, force portal
    if (low_count > 0) {
        ctx.portal_forced = true;
        LOG_INFO(TAG, "GPIO0 held low - forced setup portal");
    }
}

// WiFi event handler (ESP-IDF level)
static void wifi_event_handler(int32_t event_id, void* data) {
    switch (event_id) {
        case WIFI_EVENT_STA_CONNECTED:
            ctx.state = NET_STATE_CONNECTING; // Got connected to AP, awaiting IP
            ctx.retry_count = 0;
            LOG_INFO(TAG, "WiFi station connected to AP");
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*)data;

            if (ctx.retry_count < MAX_RETRY_ATTEMPTS) {
                ctx.state = NET_STATE_DISCONNECTED;
                ctx.retry_count++;
                int backoff_ms = compute_backoff_ms(ctx.retry_count);
                LOG_INFO(TAG, "WiFi disconnected, retry %d/%d after %dms (reason: %d)",
                        ctx.retry_count, MAX_RETRY_ATTEMPTS, backoff_ms, disconnected->reason);

                // Backoff delay before retry
                vTaskDelay(pdMS_TO_TICKS(backoff_ms));
                esp_wifi_connect();
            } else {
                LOG_WARN(TAG, "Max retry attempts reached, starting SoftAP fallback");
                ctx.state = NET_STATE_DISCONNECTED;

                // Start SoftAP fallback (provisioning portal)
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
            ctx.state = NET_STATE_STATION;
            LOG_INFO(TAG, "Got IP: %s", ip_str);
            if (ctx.event_callback) {
                ctx.event_callback(LUX_WIFI_EVENT_STA_GOT_IP, &event->ip_info);
            }
            break;
        }

        case IP_EVENT_STA_LOST_IP:
            ctx.state = NET_STATE_DISCONNECTED;
            LOG_WARN(TAG, "Lost IP address");
            if (ctx.event_callback) {
                ctx.event_callback(LUX_WIFI_EVENT_STA_DISCONNECTED, NULL);
            }
            break;

        case WIFI_EVENT_AP_START:
            ctx.state = NET_STATE_AP_ONLY;
            ctx.softap_active = true;
            LOG_INFO(TAG, "SoftAP started");

            // Start captive portal
            captive_portal_start();
            if (ctx.event_callback) {
                ctx.event_callback(LUX_WIFI_EVENT_AP_STARTED, NULL);
            }
            break;

        case WIFI_EVENT_AP_STACONNECTED:
            LOG_INFO(TAG, "Station connected to SoftAP");
            if (ctx.event_callback) {
                ctx.event_callback(LUX_WIFI_EVENT_AP_STACONNECTED, NULL);
            }
            break;

        case WIFI_EVENT_AP_STADISCONNECTED:
            LOG_INFO(TAG, "Station disconnected from SoftAP");
            if (ctx.event_callback) {
                ctx.event_callback(LUX_WIFI_EVENT_AP_STADISCONNECTED, data);
            }
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

    // Check for forced portal (GPIO0 held low at boot)
    check_forced_portal(NULL);

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

    // Set WiFi mode to station + AP (AP may be active or not)
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // Configure SoftAP (will be started when needed)
    wifi_config_t ap_config = {
        .ap = {
            .ssid = DEFAULT_SOFTAP_SSID,
            .ssid_len = strlen(DEFAULT_SOFTAP_SSID),
            .password = DEFAULT_SOFTAP_PASSWORD,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN // Open for convenience
        }
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    // Configure station (empty - will be set by wifi_sta_connect)
    wifi_config_t sta_config = {0};
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    LOG_INFO(TAG, "WiFi manager initialized");
    return ESP_OK;
}

// Connect to WiFi station (from stored config)
esp_err_t wifi_sta_connect_from_config(void) {
    if (!wifi_config_exists()) {
        LOG_INFO(TAG, "No WiFi credentials in NVS");
        return ESP_ERR_NOT_FOUND;
    }

    char ssid[32] = {0};
    char password[64] = {0};
    esp_err_t err = wifi_config_load(ssid, sizeof(ssid), password, sizeof(password));
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to load WiFi credentials: %s", esp_err_to_name(err));
        return err;
    }

    return wifi_sta_connect(ssid, password);
}

// Connect to a specific WiFi network
esp_err_t wifi_sta_connect(const char* ssid, const char* password) {
    if (!ssid) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(ctx.mutex, portMAX_DELAY) != pdTRUE) {
        LOG_ERROR(TAG, "Failed to acquire WiFi manager mutex");
        return ESP_ERR_TIMEOUT;
    }

    // Save configuration to NVS
    esp_err_t err = wifi_config_save(ssid, password ? password : "");
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to save WiFi configuration");
        xSemaphoreGive(ctx.mutex);
        return err;
    }

    // Configure station
    wifi_config_t sta_config = {0};
    strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    if (password) {
        strncpy((char*)sta_config.sta.password, password, sizeof(sta_config.sta.password) - 1);
    }

    // Set WiFi configuration
    err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to set WiFi configuration: %s", esp_err_to_name(err));
        xSemaphoreGive(ctx.mutex);
        return err;
    }

    // Start connection
    ctx.state = NET_STATE_CONNECTING;
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
    if (!ssid) {
        return ESP_ERR_INVALID_ARG;
    }

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

    // Ensure AP is enabled in the mode
    if (!ctx.softap_active) {
        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (err != ESP_OK) {
            LOG_ERROR(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(err));
            xSemaphoreGive(ctx.mutex);
            return err;
        }
        ctx.softap_active = true;
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

// Check if WiFi is connected as station
bool wifi_is_connected(void) {
    return ctx.state == NET_STATE_STATION || ctx.state == NET_STATE_AP_AND_STA;
}

// Get IP information
esp_err_t wifi_get_ip_info(char* ip, char* netmask, char* gateway) {
    if (ctx.state != NET_STATE_STATION && ctx.state != NET_STATE_AP_AND_STA) {
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

// Get current network state
net_state_t wifi_get_net_state(void) {
    return ctx.state;
}

// Check if we should enter provisioning portal
bool wifi_should_enter_portal(void) {
    // Enter portal if: no credentials stored, or forced via GPIO0,
    // or station failed to connect after retries
    if (ctx.portal_forced) {
        return true;
    }
    if (!wifi_config_exists()) {
        return true;
    }
    if (ctx.retry_count >= MAX_RETRY_ATTEMPTS && !ctx.softap_active) {
        return true;
    }
    return false;
}
