#pragma once
// Minimal RMT TX/shim for host tests — only types needed for compilation.
#include <stdint.h>
#include <stddef.h>

typedef int rmt_channel_handle_t;
typedef int rmt_encoder_handle_t;
typedef int rmt_symbol_word_t;
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NOT_FOUND -1
#define ESP_ERR_INVALID_ARG -2

typedef struct {
    int gpio_num;
    int clk_out_hz;
    int mem_block_symbols;
    struct { uint8_t with_dma : 1; } flags;
} rmt_tx_channel_config_t;

typedef struct {
    uint16_t duration0;
    uint8_t  level0;
    uint16_t duration1;
    uint8_t  level1;
} rmt_symbol_word_t_real;
