#include "config_engine.h"
#include "config_schema.h"
#include "common.h"
#include "logger.h"
#include "hw.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char* TAG = "config_engine";
static const char* NVS_NAMESPACE = "dmxgw";
static bool config_initialized = false;

// Initialize configuration engine
esp_err_t config_engine_init(void) {
    if (config_initialized) {
        return ESP_OK;
    }

    // Initialize configuration schema
    ESP_ERROR_CHECK(config_schema_init());

    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    config_initialized = true;
    LOG_INFO(TAG, "Configuration engine initialized");
    return ESP_OK;
}

// Reset configuration to template defaults
esp_err_t config_reset_to_template(void) {
    if (!config_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Get board type
    board_type_t board = hw_get_board_type();
    const config_values_t* template = config_get_board_template(board);
    const config_values_t* defaults = config_get_defaults();

    // Get pointer to config values
    config_values_t* values = config_get_values();

    // Apply defaults first
    memcpy(values, defaults, sizeof(config_values_t));

    // Apply board template overrides
    if (template) {
        if (strlen(template->hostname) > 0) {
            strncpy(values->hostname, template->hostname, sizeof(values->hostname) - 1);
        }
        if (template->wifi_mode >= 0) {
            values->wifi_mode = template->wifi_mode;
        }
        if (template->led_brightness >= 0) {
            values->led_brightness = template->led_brightness;
        }
        if (template->led_pattern >= 0) {
            values->led_pattern = template->led_pattern;
        }
        if (template->log_level >= 0) {
            values->log_level = template->log_level;
        }
        if (template->led_pin >= 0) {
            values->led_pin = template->led_pin;
        }
        if (template->led_type >= 0) {
            values->led_type = template->led_type;
        }
    }

    LOG_INFO(TAG, "Configuration reset to template");
    return ESP_OK;
}

// Load configuration from NVS
esp_err_t config_load(void) {
    if (!config_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Reset to template first
    ESP_ERROR_CHECK(config_reset_to_template());

    // Open NVS namespace
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "No configuration found in NVS, using template defaults");
        return ESP_ERR_NOT_FOUND;
    }

    // Load each configuration field
    size_t field_count;
    const cfg_field_t* fields = config_get_fields(&field_count);

    for (size_t i = 0; i < field_count; i++) {
        const cfg_field_t* field = &fields[i];

        switch (field->type) {
            case CFG_TYPE_INT: {
                int32_t value;
                err = nvs_get_i32(nvs_handle, field->key, &value);
                if (err == ESP_OK) {
                    if (value >= field->min && value <= field->max) {
                        *(int*)field->value_ptr = (int)value;
                    } else {
                        LOG_WARN(TAG, "Value for %s out of range, using default", field->key);
                    }
                }
                break;
            }
            case CFG_TYPE_BOOL: {
                uint8_t value;
                err = nvs_get_u8(nvs_handle, field->key, &value);
                if (err == ESP_OK) {
                    *(bool*)field->value_ptr = value ? true : false;
                }
                break;
            }
            case CFG_TYPE_STRING: {
                char value[256];
                size_t length = sizeof(value);
                err = nvs_get_str(nvs_handle, field->key, value, &length);
                if (err == ESP_OK) {
                    strncpy((char*)field->value_ptr, value, field->max);
                    ((char*)field->value_ptr)[field->max - 1] = '\0';
                }
                break;
            }
            case CFG_TYPE_ENUM: {
                int32_t value;
                err = nvs_get_i32(nvs_handle, field->key, &value);
                if (err == ESP_OK) {
                    if (value >= field->min && value <= field->max) {
                        *(int*)field->value_ptr = (int)value;
                    }
                }
                break;
            }
        }
    }

    nvs_close(nvs_handle);
    LOG_INFO(TAG, "Configuration loaded from NVS");
    return ESP_OK;
}

// Save configuration to NVS
esp_err_t config_save(void) {
    if (!config_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Open NVS namespace
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    // Save each configuration field
    size_t field_count;
    const cfg_field_t* fields = config_get_fields(&field_count);

    for (size_t i = 0; i < field_count; i++) {
        const cfg_field_t* field = &fields[i];

        switch (field->type) {
            case CFG_TYPE_INT:
                err = nvs_set_i32(nvs_handle, field->key, *(int*)field->value_ptr);
                break;
            case CFG_TYPE_BOOL:
                err = nvs_set_u8(nvs_handle, field->key, *(bool*)field->value_ptr ? 1 : 0);
                break;
            case CFG_TYPE_STRING:
                err = nvs_set_str(nvs_handle, field->key, (char*)field->value_ptr);
                break;
            case CFG_TYPE_ENUM:
                err = nvs_set_i32(nvs_handle, field->key, *(int*)field->value_ptr);
                break;
        }

        if (err != ESP_OK) {
            LOG_ERROR(TAG, "Failed to save %s: %s", field->key, esp_err_to_name(err));
            nvs_close(nvs_handle);
            return err;
        }
    }

    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);
    LOG_INFO(TAG, "Configuration saved to NVS");
    return ESP_OK;
}

// Set configuration value
esp_err_t config_set_value(const char* key, const char* value) {
    if (!config_initialized || !key || !value) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t field_count;
    const cfg_field_t* fields = config_get_fields(&field_count);

    for (size_t i = 0; i < field_count; i++) {
        if (strcmp(fields[i].key, key) == 0) {
            // Validate value
            esp_err_t err = config_validate_value(&fields[i], value);
            if (err != ESP_OK) {
                LOG_ERROR(TAG, "Invalid value for %s: %s", key, value);
                return err;
            }

            // Set value based on type
            switch (fields[i].type) {
                case CFG_TYPE_INT: {
                    char* endptr;
                    int int_val = strtol(value, &endptr, 0);
                    *(int*)fields[i].value_ptr = int_val;
                    break;
                }
                case CFG_TYPE_BOOL: {
                    bool bool_val = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
                    *(bool*)fields[i].value_ptr = bool_val;
                    break;
                }
                case CFG_TYPE_STRING:
                    strncpy((char*)fields[i].value_ptr, value, fields[i].max - 1);
                    ((char*)fields[i].value_ptr)[fields[i].max - 1] = '\0';
                    break;
                case CFG_TYPE_ENUM: {
                    char* endptr;
                    int enum_val = strtol(value, &endptr, 0);
                    *(int*)fields[i].value_ptr = enum_val;
                    break;
                }
            }

            LOG_INFO(TAG, "Set %s = %s", key, value);
            return ESP_OK;
        }
    }

    LOG_ERROR(TAG, "Configuration key not found: %s", key);
    return ESP_ERR_NOT_FOUND;
}

// Get configuration value
char* config_get_value(const char* key) {
    if (!config_initialized || !key) {
        return NULL;
    }

    size_t field_count;
    const cfg_field_t* fields = config_get_fields(&field_count);

    for (size_t i = 0; i < field_count; i++) {
        if (strcmp(fields[i].key, key) == 0) {
            char* value_str = malloc(256);
            if (!value_str) {
                return NULL;
            }

            switch (fields[i].type) {
                case CFG_TYPE_INT:
                    snprintf(value_str, 256, "%d", *(int*)fields[i].value_ptr);
                    break;
                case CFG_TYPE_BOOL:
                    snprintf(value_str, 256, "%s", *(bool*)fields[i].value_ptr ? "true" : "false");
                    break;
                case CFG_TYPE_STRING:
                    snprintf(value_str, 256, "%s", (char*)fields[i].value_ptr);
                    break;
                case CFG_TYPE_ENUM:
                    snprintf(value_str, 256, "%d", *(int*)fields[i].value_ptr);
                    break;
            }

            return value_str;
        }
    }

    LOG_ERROR(TAG, "Configuration key not found: %s", key);
    return NULL;
}