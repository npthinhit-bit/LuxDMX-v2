#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t config_serial_handle_command(const char* command, char* response, size_t response_len);

// Check if a reboot was requested via the serial console
// Returns true and clears the flag, should be called by the main loop
bool config_serial_check_reboot(void);

#ifdef __cplusplus
}
#endif
