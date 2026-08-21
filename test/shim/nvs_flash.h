/*
 * Native test shim - NVS Flash
 */
#pragma once

#include "nvs.h"
#include "esp_err.h"

esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
