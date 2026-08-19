/*
 * Native test shim - GPIO driver
 */
#pragma once

#include <stdint.h>
#include <string.h>

typedef int gpio_num_t;
#define GPIO_NUM_0 0
#define GPIO_NUM_1 1
#define GPIO_NUM_2 2
#define GPIO_NUM_48 48

#define GPIO_MODE_INPUT    0
#define GPIO_MODE_OUTPUT   1
#define GPIO_PULLUP_DISABLE  0
#define GPIO_PULLUP_ENABLE   1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_PULLDOWN_ENABLE  1
#define GPIO_INTR_DISABLE 0

typedef struct {
    uint64_t pin_bit_mask;
    int mode;
    int pull_up_en;
    int pull_down_en;
    int intr_type;
} gpio_config_t;

void gpio_config(const gpio_config_t* config);
void gpio_set_level(gpio_num_t gpio_num, int level);
int gpio_get_level(gpio_num_t gpio_num);
