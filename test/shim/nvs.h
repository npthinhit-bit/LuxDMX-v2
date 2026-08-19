/*
 * Native test shim - NVS
 * Uses a simple in-memory key-value store for testing
 */
#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

typedef void* nvs_handle_t;

#define NVS_READONLY 0
#define NVS_READWRITE 1

esp_err_t nvs_open(const char* namespace, int open_mode, nvs_handle_t* out_handle);
esp_err_t nvs_close(nvs_handle_t handle);
esp_err_t nvs_set_str(nvs_handle_t handle, const char* key, const char* value);
esp_err_t nvs_get_str(nvs_handle_t handle, const char* key, char* out_str, size_t* length);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char* key);
esp_err_t nvs_set_i32(nvs_handle_t handle, const char* key, int32_t value);
esp_err_t nvs_get_i32(nvs_handle_t handle, const char* key, int32_t* out_value);
esp_err_t nvs_set_u8(nvs_handle_t handle, const char* key, uint8_t value);
esp_err_t nvs_get_u8(nvs_handle_t handle, const char* key, uint8_t* out_value);
