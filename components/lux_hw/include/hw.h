#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "boards.h"

#ifdef __cplusplus
extern "C" {
#endif

// Board configuration structure
typedef struct {
    board_type_t type;
    const char* name;
    led_type_t led_type;
    gpio_num_t led_gpio;       // GPIO number (cast from int in board_def_t)
    net_if_type_t net_if;
    uint32_t capabilities;
    board_pin_map_t pins;
} board_config_t;

// Hardware abstraction interface
esp_err_t hw_init(void);
board_type_t hw_get_board_type(void);
const board_config_t* hw_get_board_config(void);

#ifdef __cplusplus
}
#endif
