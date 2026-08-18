#pragma once

#include "esp_err.h"

// LED pattern types
typedef enum {
    LED_PATTERN_BOOT,
    LED_PATTERN_WIFI_CONNECTING,
    LED_PATTERN_WIFI_CONNECTED,
    LED_PATTERN_AP_ACTIVE,
    LED_PATTERN_ERROR,
    LED_PATTERN_OFF
} led_pattern_t;

// LED driver interface
typedef struct {
    esp_err_t (*init)(void);
    esp_err_t (*set_pattern)(led_pattern_t pattern);
    esp_err_t (*set_brightness)(uint8_t brightness);
    esp_err_t (*update)(void);
} led_driver_t;

// LED driver factory function
led_driver_t* led_driver_create(void);