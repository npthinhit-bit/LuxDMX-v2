#include "wifi_config.h"
#include "logger.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "wifi_config";
static const char* NVS_NAMESPACE = "wifi_config";
static const char* NVS_KEY_SSID = "ssid";
static const char* NVS_KEY_PASSWORD = "password";

esp_err_t wifi_config_save(const char* ssid, const char* password) {
    if (!ssid) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(nvs_handle, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to save SSID: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    if (password && strlen(password) > 0) {
        err = nvs_set_str(nvs_handle, NVS_KEY_PASSWORD, password);
        if (err != ESP_OK) {
            LOG_ERROR(TAG, "Failed to save password: %s", esp_err_to_name(err));
            nvs_close(nvs_handle);
            return err;
        }
    } else {
        // Remove password key if empty
        nvs_erase_key(nvs_handle, NVS_KEY_PASSWORD);
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    } else {
        LOG_INFO(TAG, "WiFi config saved: SSID=%s", ssid);
    }

    nvs_close(nvs_handle);
    return err;
}

esp_err_t wifi_config_load(char* ssid, size_t ssid_len, char* password, size_t password_len) {
    if (!ssid || ssid_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "No WiFi config in NVS");
        return err;
    }

    size_t len = ssid_len;
    err = nvs_get_str(nvs_handle, NVS_KEY_SSID, ssid, &len);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "No SSID in NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    if (password && password_len > 0) {
        len = password_len;
        err = nvs_get_str(nvs_handle, NVS_KEY_PASSWORD, password, &len);
        if (err != ESP_OK) {
            password[0] = '\0';
        }
    }

    nvs_close(nvs_handle);
    LOG_INFO(TAG, "WiFi config loaded: SSID=%s", ssid);
    return ESP_OK;
}

bool wifi_config_exists(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return false;
    }

    char ssid[32];
    size_t len = sizeof(ssid);
    err = nvs_get_str(nvs_handle, NVS_KEY_SSID, ssid, &len);
    nvs_close(nvs_handle);

    return err == ESP_OK && strlen(ssid) > 0;
}