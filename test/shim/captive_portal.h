/*
 * Native test shim - Captive portal
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t captive_portal_start(void);
esp_err_t captive_portal_stop(void);
bool captive_portal_is_active(void);
