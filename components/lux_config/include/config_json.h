#pragma once

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t config_export_json_string(char** json_string);
esp_err_t config_import_json_string(const char* json_string);

#ifdef __cplusplus
}
#endif
