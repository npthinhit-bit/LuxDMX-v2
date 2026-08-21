#include "led_driver.h"
#include "hw.h"
#include "common.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "logger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
            state = (esp_log_timestamp() / 200) % 2;
            break;
        case LED_PATTERN_WIFI_CONNECTING:
            state = (esp_log_timestamp() / 100) % 2;
            break;
        case LED_PATTERN_WIFI_CONNECTED:
            state = true;
            break;
        case LED_PATTERN_AP_ACTIVE:
            state = (esp_log_timestamp() / 500) % 2;
            break;
        case LED_PATTERN_ERROR:
            state = ((esp_log_timestamp() / 100) % 4) < 2;
            break;
        case LED_PATTERN_OFF:
            state = false;
            break;
    }

    gpio_set_level(led->gpio, state ? 1 : 0);
    return ESP_OK;
}

// WS2812 LED implementation using GPIO with precise timing
typedef struct {
    led_driver_t base;
    gpio_num_t gpio;
    led_pattern_t current_pattern;
    uint8_t brightness;
} ws2812_led_t;

// WS2812 timing - at 240MHz CPU, each iteration is ~4.17ns
// We aim for: T0H=0.4us, T1H=0.8us, T0L=0.85us, T1L=0.45us
// Using esp_rom_delay_us provides us with microsecond delays
static inline void ws2812_write_bit(gpio_num_t gpio, uint8_t bit) {
    if (bit) {
        // Logical 1: 0.8us high, 0.45us low
        gpio_set_level(gpio, 1);
        esp_rom_delay_us(0.8);
        gpio_set_level(gpio, 0);
        esp_rom_delay_us(0.45);
    } else {
        // Logical 0: 0.4us high, 0.85us low
        gpio_set_level(gpio, 1);
        esp_rom_delay_us(0.4);
        gpio_set_level(gpio, 0);
        esp_rom_delay_us(0.85);
    }
}

static void ws2812_write_byte(gpio_num_t gpio, uint8_t byte) {
    // WS2812 uses GRB order
    for (int i = 7; i >= 0; i--) {
        ws2812_write_bit(gpio, (byte >> i) & 1);
    }
}

static void ws2812_set_pixel(gpio_num_t gpio, uint8_t r, uint8_t g, uint8_t b) {
    ws2812_write_byte(gpio, g);  // Green first
    ws2812_write_byte(gpio, r);  // Red second
    ws2812_write_byte(gpio, b);  // Blue third
    // Reset pulse
    gpio_set_level(gpio, 0);
    esp_rom_delay_us(50);
}

static esp_err_t ws2812_init(led_driver_t* driver) {
    ws2812_led_t* led = (ws2812_led_t*)driver;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << led->gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(led->gpio, 0);

    led->current_pattern = LED_PATTERN_BOOT;
    led->brightness = 100;
    LOG_INFO(TAG, "WS2812 LED initialized on GPIO %d", led->gpio);
    return ESP_OK;
}

static esp_err_t ws2812_set_pattern(led_driver_t* driver, led_pattern_t pattern) {
    ws2812_led_t* led = (ws2812_led_t*)driver;
    led->current_pattern = pattern;
    return ESP_OK;
}

static esp_err_t ws2812_set_brightness(led_driver_t* driver, uint8_t brightness) {
    ws2812_led_t* led = (ws2812_led_t*)driver;
    led->brightness = brightness;
    return ESP_OK;
}

static esp_err_t ws2812_update(led_driver_t* driver) {
    ws2812_led_t* led = (ws2812_led_t*)driver;
    uint8_t r = 0, g = 0, b = 0;
    bool on = true;

    uint8_t scale = led->brightness;

    switch (led->current_pattern) {
        case LED_PATTERN_BOOT:
            r = 0; g = 8; b = 0; break;
        case LED_PATTERN_WIFI_CONNECTING:
            r = 8; g = 0; b = 8; break;
        case LED_PATTERN_WIFI_CONNECTED:
            r = 0; g = 8; b = 0; break;
        case LED_PATTERN_AP_ACTIVE:
            r = 8; g = 0; b = 8; break;
        case LED_PATTERN_ERROR:
            on = ((esp_log_timestamp() / 100) % 4) < 2;
            r = 8; g = 0; b = 0; break;
        case LED_PATTERN_OFF:
            on = false; break;
    }

    if (!on) {
        r = 0; g = 0; b = 0;
    }

    // Apply brightness scaling
    r = r * scale / 100;
    g = g * scale / 100;
    b = b * scale / 100;

    ws2812_set_pixel(led->gpio, r, g, b);
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
    else if (board->led_type == LED_TYPE_WS2812) {
        ws2812_led_t* led = calloc(1, sizeof(ws2812_led_t));
        if (!led) {
            return NULL;
        }

        led->base.init = ws2812_init;
        led->base.set_pattern = ws2812_set_pattern;
        led->base.set_brightness = ws2812_set_brightness;
        led->base.update = ws2812_update;
        led->gpio = board->led_gpio;
        led->brightness = 100;
        led->current_pattern = LED_PATTERN_BOOT;

        return &led->base;
    }
    else {
        LOG_WARN(TAG, "Unsupported LED type %d, using simple GPIO fallback", board->led_type);
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