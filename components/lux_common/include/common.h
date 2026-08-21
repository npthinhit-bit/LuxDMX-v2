#pragma once

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

// Common error codes
typedef enum {
    LUX_OK = 0,               // Success
    LUX_ERR = -1,             // Generic error
    LUX_ERR_INVALID_ARG = -2, // Invalid argument
    LUX_ERR_NO_MEM = -3,      // Out of memory
    LUX_ERR_TIMEOUT = -4,     // Timeout
    LUX_ERR_NOT_FOUND = -5,   // Resource not found
    LUX_ERR_INVALID_STATE = -6, // Invalid state
} lux_err_t;

// Common utilities
void lux_delay_ms(uint32_t ms);
void lux_hexdump(const void *mem, uint32_t len, uint8_t cols);
char* lux_strdup(const char* str);

#ifdef __cplusplus
}
#endif
