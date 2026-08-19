#include "config_engine.h"
#include "common.h"
#include "logger.h"
#include "hw.h"
#include "boards.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "config_schema";

// Configuration field table
static cfg_field_t config_fields[] = {
    // WiFi configuration
    {
        .key = "wifi_ssid",
        .json_key = "wifiSsid",
        .type = CFG_TYPE_STRING,
        .value_ptr = NULL, // Will be set in config_engine_init
        .min = 0,
        .max = 32,
        .label = "WiFi SSID",
        .group = "Network",
        .flags = CFG_FLAG_LIVE
    },
    {
        .key = "wifi_password",
        .json_key = "wifiPassword",
        .type = CFG_TYPE_STRING,
        .value_ptr = NULL,
        .min = 0,
        .max = 64,
        .label = "WiFi Password",
        .group = "Network",
        .flags = CFG_FLAG_LIVE | CFG_FLAG_SECRET
    },
    {
        .key = "wifi_mode",
        .json_key = "wifiMode",
        .type = CFG_TYPE_INT,
        .value_ptr = NULL,
        .min = 0,
        .max = 2,
        .label = "WiFi Mode",
        .group = "Network",
        .flags = CFG_FLAG_LIVE
    },
    {
        .key = "hostname",
        .json_key = "hostname",
        .type = CFG_TYPE_STRING,
        .value_ptr = NULL,
        .min = 0,
        .max = 32,
        .label = "Hostname",
        .group = "Network",
        .flags = CFG_FLAG_REBOOT
    },

    // LED configuration
    {
        .key = "led_brightness",
        .json_key = "ledBrightness",
        .type = CFG_TYPE_INT,
        .value_ptr = NULL,
        .min = 0,
        .max = 100,
        .label = "LED Brightness",
        .group = "LED",
        .flags = CFG_FLAG_LIVE
    },
    {
        .key = "led_pattern",
        .json_key = "ledPattern",
        .type = CFG_TYPE_INT,
        .value_ptr = NULL,
        .min = 0,
        .max = 5,
        .label = "LED Pattern",
        .group = "LED",
        .flags = CFG_FLAG_LIVE
    },

    // System configuration
    {
        .key = "log_level",
        .json_key = "logLevel",
        .type = CFG_TYPE_INT,
        .value_ptr = NULL,
        .min = 0,
        .max = 4,
        .label = "Log Level",
        .group = "System",
        .flags = CFG_FLAG_LIVE
    },

    // Hardware configuration (reboot required)
    {
        .key = "led_pin",
        .json_key = "ledPin",
        .type = CFG_TYPE_INT,
        .value_ptr = NULL,
        .min = 0,
        .max = 48,
        .label = "LED Pin",
        .group = "Hardware",
        .flags = CFG_FLAG_REBOOT
    },
    {
        .key = "led_type",
        .json_key = "ledType",
        .type = CFG_TYPE_INT,
        .value_ptr = NULL,
        .min = 0,
        .max = 3,
        .label = "LED Type",
        .group = "Hardware",
        .flags = CFG_FLAG_REBOOT
    }
};

// Configuration values
typedef struct {
    char wifi_ssid[32];
    char wifi_password[64];
    int wifi_mode;
    char hostname[32];
    int led_brightness;
    int led_pattern;
    int log_level;
    int led_pin;
    int led_type;
} config_values_t;

static config_values_t config_values;
static const size_t config_field_count = sizeof(config_fields) / sizeof(config_fields[0]);

// Default configuration values
static const config_values_t default_config = {
    .wifi_ssid = "",
    .wifi_password = "",
    .wifi_mode = 0, // 0 = Station, 1 = AP, 2 = Station + AP
    .hostname = "luxdmx",
    .led_brightness = 100,
    .led_pattern = 0, // LED_PATTERN_BOOT
    .log_level = LOG_LEVEL_INFO,
    .led_pin = 2,
    .led_type = 1 // LED_TYPE_SIMPLE_GPIO
};

// Board-specific template configurations
static const config_values_t board_templates[] = {
    [BOARD_ESP32S3_N16R8] = {
        .wifi_ssid = "",
        .wifi_password = "",
        .wifi_mode = 0,
        .hostname = "luxdmx-s3",
        .led_brightness = 100,
        .led_pattern = 0,
        .log_level = LOG_LEVEL_INFO,
        .led_pin = 48,  // GPIO48 for WS2812 on ESP32-S3 DevKitC-1
        .led_type = 2   // LED_TYPE_WS2812
    },
    [BOARD_WT32ETH01] = {
        .wifi_ssid = "",
        .wifi_password = "",
        .wifi_mode = 0,
        .hostname = "luxdmx-wt32",
        .led_brightness = 100,
        .led_pattern = 0,
        .log_level = LOG_LEVEL_INFO,
        .led_pin = 2,
        .led_type = 1   // LED_TYPE_SIMPLE_GPIO
    },
    [BOARD_ESP32DEV] = {
        .wifi_ssid = "",
        .wifi_password = "",
        .wifi_mode = 0,
        .hostname = "luxdmx-esp32",
        .led_brightness = 100,
        .led_pattern = 0,
        .log_level = LOG_LEVEL_INFO,
        .led_pin = 2,
        .led_type = 1   // LED_TYPE_SIMPLE_GPIO
    }
};

// Initialize configuration schema
esp_err_t config_schema_init(void) {
    // Set value pointers for each field
    for (size_t i = 0; i < config_field_count; i++) {
        switch (i) {
            case 0: config_fields[i].value_ptr = &config_values.wifi_ssid; break;
            case 1: config_fields[i].value_ptr = &config_values.wifi_password; break;
            case 2: config_fields[i].value_ptr = &config_values.wifi_mode; break;
            case 3: config_fields[i].value_ptr = &config_values.hostname; break;
            case 4: config_fields[i].value_ptr = &config_values.led_brightness; break;
            case 5: config_fields[i].value_ptr = &config_values.led_pattern; break;
            case 6: config_fields[i].value_ptr = &config_values.log_level; break;
            case 7: config_fields[i].value_ptr = &config_values.led_pin; break;
            case 8: config_fields[i].value_ptr = &config_values.led_type; break;
        }
    }

    return ESP_OK;
}

// Get configuration fields
const cfg_field_t* config_get_fields(size_t* count) {
    if (count) {
        *count = config_field_count;
    }
    return config_fields;
}

// Get configuration field count
size_t config_get_field_count(void) {
    return config_field_count;
}

// Get pointer to config values struct
config_values_t* config_get_values(void) {
    return &config_values;
}

// Get default configuration
const config_values_t* config_get_defaults(void) {
    return &default_config;
}

// Get board template
const config_values_t* config_get_board_template(board_type_t board) {
    if (board >= BOARD_UNKNOWN) {
        return &default_config;
    }
    return &board_templates[board];
}

// Validate configuration value
esp_err_t config_validate_value(const cfg_field_t* field, const char* value) {
    if (!field || !value) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (field->type) {
        case CFG_TYPE_INT: {
            char* endptr;
            long int_val = strtol(value, &endptr, 0);
            if (*endptr != '\0') {
                return ESP_ERR_INVALID_ARG;
            }
            if (int_val < field->min || int_val > field->max) {
                return ESP_ERR_INVALID_ARG;
            }
            break;
        }
        case CFG_TYPE_BOOL: {
            if (strcmp(value, "true") != 0 && strcmp(value, "false") != 0 &&
                strcmp(value, "1") != 0 && strcmp(value, "0") != 0) {
                return ESP_ERR_INVALID_ARG;
            }
            break;
        }
        case CFG_TYPE_STRING:
            if (strlen(value) > (size_t)field->max) {
                return ESP_ERR_INVALID_ARG;
            }
            break;
        case CFG_TYPE_ENUM:
            // TODO: Implement enum validation
            break;
    }

    return ESP_OK;
}