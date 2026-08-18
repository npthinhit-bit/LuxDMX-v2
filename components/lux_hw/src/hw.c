#include "hw.h"
#include "common.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char* TAG = "hw";

// Board configurations
static const board_config_t board_configs[] = {
    [BOARD_ESP32S3_N16R8] = {
        .type = BOARD_ESP32S3_N16R8,
        .name = "ESP32-S3-N16R8",
        .led_type = LED_TYPE_WS2812,
        .led_gpio = GPIO_NUM_48,
        .net_if = NET_IF_WIFI
    },
    [BOARD_WT32ETH01] = {
        .type = BOARD_WT32ETH01,
        .name = "WT32-ETH01",
        .led_type = LED_TYPE_SIMPLE_GPIO,
        .led_gpio = GPIO_NUM_2,
        .net_if = NET_IF_ETH_RMII
    },
    [BOARD_ESP32DEV] = {
        .type = BOARD_ESP32DEV,
        .name = "ESP32-DevKit",
        .led_type = LED_TYPE_SIMPLE_GPIO,
        .led_gpio = GPIO_NUM_2,
        .net_if = NET_IF_WIFI
    }
};

static board_type_t current_board = BOARD_UNKNOWN;

// Board detection function
static board_type_t detect_board(void) {
    // Read chip info
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    // Check for ESP32-S3
    if (chip_info.model == CHIP_ESP32S3) {
        // Check if PSRAM is available (indicates N16R8 variant)
        if (esp_psram_is_initialized()) {
            return BOARD_ESP32S3_N16R8;
        }
    }
    // Check for WT32-ETH01 (ESP32 with specific GPIO configuration)
    else if (chip_info.model == CHIP_ESP32) {
        // WT32-ETH01 has specific Ethernet PHY configuration
        // This is a simplified detection - real implementation would check more
        return BOARD_WT32ETH01;
    }

    // Default to ESP32-DevKit
    return BOARD_ESP32DEV;
}

esp_err_t hw_init(void) {
    // Detect board type
    current_board = detect_board();
    LOG_INFO(TAG, "Detected board: %s", board_configs[current_board].name);

    // Configure LED GPIO as output
    if (board_configs[current_board].led_type == LED_TYPE_SIMPLE_GPIO) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << board_configs[current_board].led_gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        gpio_set_level(board_configs[current_board].led_gpio, 0);
    }

    return ESP_OK;
}

board_type_t hw_get_board_type(void) {
    return current_board;
}

const board_config_t* hw_get_board_config(void) {
    return &board_configs[current_board];
}