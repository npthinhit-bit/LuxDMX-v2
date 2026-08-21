/*
 * Native test shim - ESP System
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>

uint32_t esp_get_free_heap_size(void);
void esp_restart(void);
