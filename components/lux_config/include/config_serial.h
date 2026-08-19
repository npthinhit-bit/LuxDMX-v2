#pragma once

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t config_serial_handle_command(const char* command, char* response, size_t response_len);

#ifdef __cplusplus
}
#endif
