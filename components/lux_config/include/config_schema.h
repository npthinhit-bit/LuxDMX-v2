#pragma once

#include "config_engine.h"
#include "hw.h"

#ifdef __cplusplus
extern "C" {
#endif

// Configuration values struct (used internally by config engine)
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

// Initialize the configuration schema (sets field value pointers)
esp_err_t config_schema_init(void);

// Get pointer to the config_values struct
config_values_t* config_get_values(void);

// Get default configuration values
const config_values_t* config_get_defaults(void);

// Get board template
const config_values_t* config_get_board_template(board_type_t board);

// Validate a config value against its field descriptor
esp_err_t config_validate_value(const cfg_field_t* field, const char* value);

// Get config field count
size_t config_get_field_count(void);

#ifdef __cplusplus
}
#endif
