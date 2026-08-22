#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_MAX_OUTPUTS 4
#define BOARD_PANEL_LED_COUNT 5
#define BOARD_PIN_UNASSIGNED (-1)

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

// Capabilities describe what a board profile can provision. They do not claim
// that the corresponding runtime driver is already implemented or HIL-tested.
typedef enum {
    BOARD_CAP_WIFI       = (1u << 0),
    BOARD_CAP_ETH_SPI    = (1u << 1),
    BOARD_CAP_ETH_RMII   = (1u << 2),
    BOARD_CAP_PSRAM      = (1u << 3),
    BOARD_CAP_WS2812     = (1u << 4),
    BOARD_CAP_DISPLAY    = (1u << 5),
    BOARD_CAP_ENCODER    = (1u << 6),
    BOARD_CAP_RDM        = (1u << 7),
} board_capability_t;

// Canonical board pin map. BOARD_PIN_UNASSIGNED is intentional: it prevents a
// driver from treating an undocumented or unverified pin as production truth.
typedef struct {
    int boot_pin;
    int console_uart;
    int status_led;
    int panel_led[BOARD_PANEL_LED_COUNT];
    int dmx_tx[BOARD_MAX_OUTPUTS];
    int dmx_rx[BOARD_MAX_OUTPUTS];
    int dmx_rts[BOARD_MAX_OUTPUTS];
    int display_sda;
    int display_scl;
    int w5500_cs;
    int w5500_sck;
    int w5500_mosi;
    int w5500_miso;
    int w5500_int;
    int w5500_rst;
    int rmii_mdc;
    int rmii_mdio;
    int rmii_power;
} board_pin_map_t;

// Board configuration table entry
// Indexed by board_type_t; BOARD_UNKNOWN is the defensive fallback entry.
typedef struct {
    board_type_t type;
    const char* name;
    const char* template_name;  // matches LUXDMX_DEFAULT_TEMPLATE build flag
    led_type_t led_type;
    int led_pin;                // compatibility alias for pins.status_led
    net_if_type_t net_if;
    int has_psram;              // compatibility field; use capabilities for new code
    uint32_t capabilities;
    board_pin_map_t pins;
} board_def_t;

// Get the board definition table (indexed by board_type_t)
const board_def_t* board_get_table(void);

// Get the number of boards
size_t board_get_count(void);

// Find a board by template name (returns BOARD_UNKNOWN if not found)
board_type_t board_find_by_template(const char* template_name);

// Return whether a board definition advertises a capability.
int board_has_capability(const board_def_t* board, board_capability_t capability);

#ifdef __cplusplus
}
#endif
