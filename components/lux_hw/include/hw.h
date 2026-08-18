#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/rmt.h"

// Board types
typedef enum {
    BOARD_ESP32S3_N16R8,
    BOARD_WT32ETH01,
    BOARD_ESP32DEV,
    BOARD_UNKNOWN
} board_type_t;

// LED types
typedef enum {
    LED_TYPE_SIMPLE_GPIO,
    LED_TYPE_WS2812,
    LED_TYPE_PANEL_5LED
} led_type_t;

// Network interface types
typedef enum {
    NET_IF_WIFI,
    NET_IF_ETH_SPI,
    NET_IF_ETH_RMII
} net_if_type_t;

// Board configuration structure
typedef struct {
    board_type_t type;
    const char* name;
    led_type_t led_type;
    gpio_num_t led_gpio;
    net_if_type_t net_if;
    // Add more board-specific configurations as needed
} board_config_t;

// Hardware abstraction interface
esp_err_t hw_init(void);
board_type_t hw_get_board_type(void);
const board_config_t* hw_get_board_config(void);