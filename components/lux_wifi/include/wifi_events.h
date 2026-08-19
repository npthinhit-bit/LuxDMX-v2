#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wifi_event_cb_t)(int32_t event_id, void* event_data);

// Initialize WiFi events subsystem with a callback that receives ESP-IDF events
esp_err_t wifi_events_init(wifi_event_cb_t cb);

#ifdef __cplusplus
}
#endif
