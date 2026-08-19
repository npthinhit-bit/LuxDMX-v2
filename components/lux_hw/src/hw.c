#include "hw.h"
#include "boards.h"
#include "common.h"
#include "logger.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"

static const char* TAG = "hw";

static board_type_t current_board = BOARD_UNKNOWN;

// Board configuration derived from the canonical board table
static board_config_t current_config = {0};

// Board detection function
static board_type_t detect_board(void) {
    // Read chip info
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    // Check for ESP32-S3
    if (chip_info.model == CHIP_ESP32S3) {
        // Check if PSRAM is available (indicates N16R8 variant)
#if CONFIG_SPIRAM
        if (esp_psram_is_initialized()) {
            return BOARD_ESP32S3_N16R8;
        }
#else
        // PSRAM not compiled in - check via heap caps availability
        if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0) {
            return BOARD_ESP32S3_N16R8;
        }
#endif
        // Default S3 to DevKit
        return BOARD_ESP32DEV;
    }

    // Check for WT32-ETH01 (ESP32-WROVER module has PSRAM)
    if (chip_info.model == CHIP_ESP32) {
        // WT32-ETH01 uses ESP32-WROVER with PSRAM
        // Check for PSRAM
#if CONFIG_SPIRAM
        if (esp_psram_is_initialized()) {
            return BOARD_WT32ETH01;
        }
#else
        if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0) {
            return BOARD_WT32ETH01;
        }
#endif
        // Default ESP32 to DevKit
        return BOARD_ESP32DEV;
    }

    // Default to ESP32-DevKit
    return BOARD_ESP32DEV;
}

esp_err_t hw_init(void) {
    // Detect board type
    current_board = detect_board();

    // Get board definition from the canonical table
    const board_def_t* def = &board_get_table()[current_board];
    LOG_INFO(TAG, "Detected board: %s", def->name);

    // Populate the config structure
    current_config.type = def->type;
    current_config.name = def->name;
    current_config.led_type = def->led_type;
    current_config.led_gpio = (gpio_num_t)def->led_pin;
    current_config.net_if = def->net_if;

    // Configure LED GPIO as output for simple GPIO type
    if (current_config.led_type == LED_TYPE_SIMPLE_GPIO) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << current_config.led_gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        gpio_set_level(current_config.led_gpio, 0);
    }

    return ESP_OK;
}

board_type_t hw_get_board_type(void) {
    return current_board;
}

const board_config_t* hw_get_board_config(void) {
    return &current_config;
}
