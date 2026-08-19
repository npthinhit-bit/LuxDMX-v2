#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Save WiFi configuration to NVS
esp_err_t wifi_config_save(const char* ssid, const char* password);

// Load WiFi configuration from NVS
esp_err_t wifi_config_load(char* ssid, size_t ssid_len, char* password, size_t password_len);

// Check if WiFi credentials exist in NVS
bool wifi_config_exists(void);

#ifdef __cplusplus
}
#endif
