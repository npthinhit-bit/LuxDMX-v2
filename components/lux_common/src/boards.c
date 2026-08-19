#include "boards.h"
#include "logger.h"
#include <string.h>

static const char* TAG = "boards";

// Canonical board definition table (gap-a closed per REFACTOR_PLAN §5.2)
// Indexed by board_type_t
static const board_def_t board_table[] = {
    [BOARD_ESP32S3_N16R8] = {
        .type = BOARD_ESP32S3_N16R8,
        .name = "ESP32-S3-WROOM-2 N16R8",
        .template_name = "esp32s3_psram",
        .led_type = LED_TYPE_WS2812,
        .led_pin = 48,
        .net_if = NET_IF_WIFI,
        .has_psram = 1,
    },
    [BOARD_WT32ETH01] = {
        .type = BOARD_WT32ETH01,
        .name = "WT32-ETH01",
        .template_name = "wt32eth01",
        .led_type = LED_TYPE_SIMPLE_GPIO,
        .led_pin = 2,
        .net_if = NET_IF_ETH_RMII,
        .has_psram = 0,
    },
    [BOARD_ESP32DEV] = {
        .type = BOARD_ESP32DEV,
        .name = "ESP32-DevKit",
        .template_name = "esp32dev",
        .led_type = LED_TYPE_SIMPLE_GPIO,
        .led_pin = 2,
        .net_if = NET_IF_WIFI,
        .has_psram = 0,
    },
    [BOARD_UNKNOWN] = {
        .type = BOARD_UNKNOWN,
        .name = "Unknown",
        .template_name = "esp32dev",
        .led_type = LED_TYPE_SIMPLE_GPIO,
        .led_pin = 2,
        .net_if = NET_IF_WIFI,
        .has_psram = 0,
    },
};

static const size_t board_count = sizeof(board_table) / sizeof(board_table[0]);

const board_def_t* board_get_table(void) {
    return board_table;
}

size_t board_get_count(void) {
    return board_count;
}

board_type_t board_find_by_template(const char* template_name) {
    if (!template_name) {
        return BOARD_UNKNOWN;
    }
    for (size_t i = 0; i < board_count; i++) {
        if (strcmp(board_table[i].template_name, template_name) == 0) {
            return board_table[i].type;
        }
    }
    LOG_WARN(TAG, "Unknown template: %s, defaulting to esp32dev", template_name);
    return BOARD_ESP32DEV;
}
