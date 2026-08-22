#include "boards.h"
#include "logger.h"
#include <string.h>

static const char* TAG = "boards";

#define BOARD_PIN_MAP_DEFAULTS \
    .boot_pin = 0, \
    .console_uart = 0, \
    .panel_led = { \
        BOARD_PIN_UNASSIGNED, BOARD_PIN_UNASSIGNED, BOARD_PIN_UNASSIGNED, \
        BOARD_PIN_UNASSIGNED, BOARD_PIN_UNASSIGNED \
    }, \
    .dmx_tx = { 17, BOARD_PIN_UNASSIGNED, BOARD_PIN_UNASSIGNED, BOARD_PIN_UNASSIGNED }, \
    .dmx_rx = { 16, BOARD_PIN_UNASSIGNED, BOARD_PIN_UNASSIGNED, BOARD_PIN_UNASSIGNED }, \
    .dmx_rts = { \
        BOARD_PIN_UNASSIGNED, BOARD_PIN_UNASSIGNED, BOARD_PIN_UNASSIGNED, \
        BOARD_PIN_UNASSIGNED \
    }, \
    .display_sda = BOARD_PIN_UNASSIGNED, \
    .display_scl = BOARD_PIN_UNASSIGNED, \
    .w5500_cs = BOARD_PIN_UNASSIGNED, \
    .w5500_sck = BOARD_PIN_UNASSIGNED, \
    .w5500_mosi = BOARD_PIN_UNASSIGNED, \
    .w5500_miso = BOARD_PIN_UNASSIGNED, \
    .w5500_int = BOARD_PIN_UNASSIGNED, \
    .w5500_rst = BOARD_PIN_UNASSIGNED

// Canonical board definition table. Pin values shared by the base template are
// retained here as defaults; a missing physical/HIL decision stays unassigned.
static const board_def_t board_table[] = {
    [BOARD_ESP32S3_N16R8] = {
        .type = BOARD_ESP32S3_N16R8,
        .name = "ESP32-S3-WROOM-2 N16R8",
        .template_name = "esp32s3_psram",
        .led_type = LED_TYPE_WS2812,
        .led_pin = 48,
        .net_if = NET_IF_WIFI,
        .has_psram = 1,
        .capabilities = BOARD_CAP_WIFI | BOARD_CAP_PSRAM | BOARD_CAP_WS2812,
        .pins = {
            BOARD_PIN_MAP_DEFAULTS,
            .status_led = 48,
            .rmii_mdc = BOARD_PIN_UNASSIGNED,
            .rmii_mdio = BOARD_PIN_UNASSIGNED,
            .rmii_power = BOARD_PIN_UNASSIGNED,
        },
    },
    [BOARD_WT32ETH01] = {
        .type = BOARD_WT32ETH01,
        .name = "WT32-ETH01",
        .template_name = "wt32eth01",
        .led_type = LED_TYPE_SIMPLE_GPIO,
        .led_pin = 2,
        .net_if = NET_IF_ETH_RMII,
        .has_psram = 0,
        .capabilities = BOARD_CAP_ETH_RMII,
        .pins = {
            BOARD_PIN_MAP_DEFAULTS,
            .status_led = 2,
            .rmii_mdc = 23,
            .rmii_mdio = 18,
            .rmii_power = 16,
        },
    },
    [BOARD_ESP32DEV] = {
        .type = BOARD_ESP32DEV,
        .name = "ESP32-DevKit",
        .template_name = "esp32dev",
        .led_type = LED_TYPE_SIMPLE_GPIO,
        .led_pin = 2,
        .net_if = NET_IF_WIFI,
        .has_psram = 0,
        .capabilities = BOARD_CAP_WIFI,
        .pins = {
            BOARD_PIN_MAP_DEFAULTS,
            .status_led = 2,
            .rmii_mdc = BOARD_PIN_UNASSIGNED,
            .rmii_mdio = BOARD_PIN_UNASSIGNED,
            .rmii_power = BOARD_PIN_UNASSIGNED,
        },
    },
    [BOARD_UNKNOWN] = {
        .type = BOARD_UNKNOWN,
        .name = "Unknown",
        .template_name = "esp32dev",
        .led_type = LED_TYPE_SIMPLE_GPIO,
        .led_pin = 2,
        .net_if = NET_IF_WIFI,
        .has_psram = 0,
        .capabilities = 0,
        .pins = {
            .boot_pin = BOARD_PIN_UNASSIGNED,
            .console_uart = BOARD_PIN_UNASSIGNED,
            .status_led = BOARD_PIN_UNASSIGNED,
            .panel_led = {
                BOARD_PIN_UNASSIGNED, BOARD_PIN_UNASSIGNED, BOARD_PIN_UNASSIGNED,
                BOARD_PIN_UNASSIGNED, BOARD_PIN_UNASSIGNED
            },
            .dmx_tx = {
                BOARD_PIN_UNASSIGNED, BOARD_PIN_UNASSIGNED, BOARD_PIN_UNASSIGNED,
                BOARD_PIN_UNASSIGNED
            },
            .dmx_rx = {
                BOARD_PIN_UNASSIGNED, BOARD_PIN_UNASSIGNED, BOARD_PIN_UNASSIGNED,
                BOARD_PIN_UNASSIGNED
            },
            .dmx_rts = {
                BOARD_PIN_UNASSIGNED, BOARD_PIN_UNASSIGNED, BOARD_PIN_UNASSIGNED,
                BOARD_PIN_UNASSIGNED
            },
            .display_sda = BOARD_PIN_UNASSIGNED,
            .display_scl = BOARD_PIN_UNASSIGNED,
            .w5500_cs = BOARD_PIN_UNASSIGNED,
            .w5500_sck = BOARD_PIN_UNASSIGNED,
            .w5500_mosi = BOARD_PIN_UNASSIGNED,
            .w5500_miso = BOARD_PIN_UNASSIGNED,
            .w5500_int = BOARD_PIN_UNASSIGNED,
            .w5500_rst = BOARD_PIN_UNASSIGNED,
            .rmii_mdc = BOARD_PIN_UNASSIGNED,
            .rmii_mdio = BOARD_PIN_UNASSIGNED,
            .rmii_power = BOARD_PIN_UNASSIGNED,
        },
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

int board_has_capability(const board_def_t* board, board_capability_t capability) {
    if (!board || capability == 0) {
        return 0;
    }
    return (board->capabilities & (uint32_t)capability) != 0;
}
