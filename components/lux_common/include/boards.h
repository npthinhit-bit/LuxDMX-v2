#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Board types
typedef enum {
    BOARD_ESP32S3_N16R8,
    BOARD_WT32ETH01,
    BOARD_ESP32DEV,
    BOARD_UNKNOWN
} board_type_t;

// LED types (spec 36)
typedef enum {
    LED_TYPE_OFF,          // type 0 - no LED
    LED_TYPE_SIMPLE_GPIO,  // type 1 - plain GPIO
    LED_TYPE_WS2812,       // type 2 - WS2812 strip
    LED_TYPE_PANEL_5LED    // type 3 - 5-LED panel (Phase 7)
} led_type_t;

// Network interface types
typedef enum {
    NET_IF_WIFI,
    NET_IF_ETH_SPI,
    NET_IF_ETH_RMII
} net_if_type_t;

// Board configuration table entry
typedef struct {
    board_type_t type;
    const char* name;
    const char* template_name;  // matches LUXDMX_DEFAULT_TEMPLATE build flag
    led_type_t led_type;
    int led_pin;               // GPIO pin number
    net_if_type_t net_if;
    int has_psram;             // 1 if PSRAM present (ESP32-S3 N16R8)
} board_def_t;

// Get the board definition table (indexed by board_type_t)
const board_def_t* board_get_table(void);

// Get the number of boards
size_t board_get_count(void);

// Find a board by template name (returns BOARD_UNKNOWN if not found)
board_type_t board_find_by_template(const char* template_name);

#ifdef __cplusplus
}
#endif
