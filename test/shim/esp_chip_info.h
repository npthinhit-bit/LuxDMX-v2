/*
 * Native test shim - ESP Chip Info
 */
#pragma once

#include <stdint.h>

#define CHIP_ESP32 1
#define CHIP_ESP32_S2 2
#define CHIP_ESP32_S3  9
#define CHIP_ESP32_C3 5

typedef struct {
    int model;
    int revision;
    int single_core;
    int dual_core;
    int quad_core;
    int octo_core;
    int hyper_single_core;
    int hyper_dual_core;
    uint32_t features;
    uint32_t cores;
    uint32_t revision_num;
    uint32_t min_chip_ver;
    char chip_str[16];
} esp_chip_info_t;

void esp_chip_info(esp_chip_info_t* info);
