#include "led_driver.h"
#include "hw.h"
#include "common.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char* TAG = "led_driver";

// Simple GPIO LED implementation
typedef struct {
    led_driver_t base;
    gpio_num_t gpio;
    led_pattern_t current_pattern;
    uint8_t brightness;
} simple_led_t;

static esp_err_t simple_led_init(led_driver_t* driver) {
    simple_led_t* led = (simple_led_t*)driver;
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << led->gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(led->gpio, 0);
    return ESP_OK;
}

static esp_err_t simple_led_set_pattern(led_driver_t* driver, led_pattern_t pattern) {
    simple_led_t* led = (simple_led_t*)driver;
    led->current_pattern = pattern;
    return ESP_OK;
}

static esp_err_t simple_led_set_brightness(led_driver_t* driver, uint8_t brightness) {
    simple_led_t* led = (simple_led_t*)driver;
    led->brightness = brightness;
    return ESP_OK;
}

static esp_err_t simple_led_update(led_driver_t* driver) {
    simple_led_t* led = (simple_led_t*)driver;
    bool state = false;

    switch (led->current_pattern) {
        case LED_PATTERN_BOOT:
            // Blink pattern for boot
            state = (esp_log_timestamp() / 200) % 2;
            break;
        case LED_PATTERN_WIFI_CONNECTING:
            // Fast blink for WiFi connecting
            state = (esp_log_timestamp() / 100) % 2;
            break;
        case LED_PATTERN_WIFI_CONNECTED:
            // Solid on for WiFi connected
            state = true;
            break;
        case LED_PATTERN_AP_ACTIVE:
            // Slow blink for AP mode
            state = (esp_log_timestamp() / 500) % 2;
            break;
        case LED_PATTERN_ERROR:
            // Fast double blink for error
            state = ((esp_log_timestamp() / 100) % 4) < 2;
            break;
        case LED_PATTERN_OFF:
            state = false;
            break;
    }

    gpio_set_level(led->gpio, state ? 1 : 0);
    return ESP_OK;
}

// LED driver factory function
led_driver_t* led_driver_create(void) {
    const board_config_t* board = hw_get_board_config();

    if (board->led_type == LED_TYPE_SIMPLE_GPIO) {
        simple_led_t* led = calloc(1, sizeof(simple_led_t));
        if (!led) {
            return NULL;
        }

        led->base.init = simple_led_init;
        led->base.set_pattern = simple_led_set_pattern;
        led->base.set_brightness = simple_led_set_brightness;
        led->base.update = simple_led_update;
        led->gpio = board->led_gpio;
        led->brightness = 100;
        led->current_pattern = LED_PATTERN_BOOT;

        return &led->base;
    }
    else {
        // For other LED types, return simple GPIO implementation as fallback
        LOG_WARN(TAG, "Unsupported LED type, using simple GPIO fallback");
        simple_led_t* led = calloc(1, sizeof(simple_led_t));
        if (!led) {
            return NULL;
        }

        led->base.init = simple_led_init;
        led->base.set_pattern = simple_led_set_pattern;
        led->base.set_brightness = simple_led_set_brightness;
        led->base.update = simple_led_update;
        led->gpio = board->led_gpio;
        led->brightness = 100;
        led->current_pattern = LED_PATTERN_BOOT;

        return &led->base;
    }
}