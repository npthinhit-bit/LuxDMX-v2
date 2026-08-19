/*
 * Native test shim - ESP Event
 */
#pragma once

#include "esp_err.h"

typedef void (*esp_event_handler_t)(int32_t event_id, void* data);

esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_register(int event_base, int32_t event_id, esp_event_handler_t handler);
esp_err_t esp_event_handler_instance_register(int event_base, int32_t event_id,
                                               esp_event_handler_t handler, void* ctx);

/* Event bases */
#define WIFI_EVENT 0
#define IP_EVENT 1
#define ESP_EVENT_ANY_ID (-1)
