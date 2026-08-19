#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start captive portal DNS server and web server redirect
esp_err_t captive_portal_start(void);

// Stop captive portal
esp_err_t captive_portal_stop(void);

// Check if captive portal is active
bool captive_portal_is_active(void);

#ifdef __cplusplus
}
#endif
