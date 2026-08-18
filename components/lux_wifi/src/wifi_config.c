#include "wifi_manager.h"
#include "common.h"
#include "logger.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char* TAG = "wifi_config";
static const char* NVS_NAMESPACE = "wifi_config";

// Default WiFi configuration
typedef struct {
    char ssid[32];
    char password[64];
    bool valid;
} wifi_config_storage_t;

static wifi_config_storage_t current_config = {
    .ssid = "",
    .password = "",
    .valid = false
};

// Save WiFi configuration to NVS
esp_err_t wifi_config_save(const char* ssid, const char* password) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Open NVS namespace
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    // Save SSID
    err = nvs_set_str(nvs_handle, "ssid", ssid);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to save SSID: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // Save password
    err = nvs_set_str(nvs_handle, "password", password);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to save password: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // Set valid flag
    err = nvs_set_u8(nvs_handle, "valid", 1);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to set valid flag: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);

    // Update current config
    strncpy(current_config.ssid, ssid, sizeof(current_config.ssid) - 1);
    strncpy(current_config.password, password, sizeof(current_config.password) - 1);
    current_config.valid = true;

    LOG_INFO(TAG, "WiFi configuration saved to NVS");
    return ESP_OK;
}

// Load WiFi configuration from NVS
esp_err_t wifi_config_load(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err;
    size_t length;
    uint8_t valid = 0;

    // Open NVS namespace
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    // Check if valid configuration exists
    err = nvs_get_u8(nvs_handle, "valid", &valid);
    if (err != ESP_OK || !valid) {
        LOG_INFO(TAG, "No valid WiFi configuration found in NVS");
        nvs_close(nvs_handle);
        current_config.valid = false;
        return ESP_ERR_NOT_FOUND;
    }

    // Load SSID
    length = sizeof(current_config.ssid);
    err = nvs_get_str(nvs_handle, "ssid", current_config.ssid, &length);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to load SSID: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        current_config.valid = false;
        return err;
    }

    // Load password
    length = sizeof(current_config.password);
    err = nvs_get_str(nvs_handle, "password", current_config.password, &length);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to load password: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        current_config.valid = false;
        return err;
    }

    nvs_close(nvs_handle);
    current_config.valid = true;

    LOG_INFO(TAG, "WiFi configuration loaded from NVS: SSID=%s", current_config.ssid);
    return ESP_OK;
}

// Get current WiFi configuration
esp_err_t wifi_config_get(char* ssid, size_t ssid_len, char* password, size_t password_len) {
    if (!current_config.valid) {
        return ESP_ERR_NOT_FOUND;
    }

    if (ssid && ssid_len > 0) {
        strncpy(ssid, current_config.ssid, ssid_len - 1);
        ssid[ssid_len - 1] = '\0';
    }

    if (password && password_len > 0) {
        strncpy(password, current_config.password, password_len - 1);
        password[password_len - 1] = '\0';
    }

    return ESP_OK;
}

// Clear WiFi configuration
esp_err_t wifi_config_clear(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Open NVS namespace
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    // Erase all keys
    err = nvs_erase_all(nvs_handle);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to erase NVS keys: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);

    // Clear current config
    memset(&current_config, 0, sizeof(current_config));
    current_config.valid = false;

    LOG_INFO(TAG, "WiFi configuration cleared from NVS");
    return ESP_OK;
}