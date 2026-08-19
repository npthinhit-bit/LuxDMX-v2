/*
 * Native test shim - ESP error type and codes
 * Minimal definitions for host-side testing without ESP-IDF
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define ESP_OK          0
#define ESP_FAIL        -1
#define ESP_ERR_NO_MEM          257
#define ESP_ERR_INVALID_ARG     261
#define ESP_ERR_INVALID_STATE   262
#define ESP_ERR_NOT_FOUND       258
#define ESP_ERR_INVALID_SIZE    263
#define ESP_ERR_INVALID_VERSION 275
#define ESP_ERR_NVS_NOT_FOUND   0x1102
#define ESP_ERR_NVS_NO_FREE_PAGES 0x1103
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1104
#define ESP_ERR_TIMEOUT         266

typedef int esp_err_t;

/* Native-test stub: log but do not abort (matches host-shim semantics; section 47). */
#define ESP_ERROR_CHECK(x) do { esp_err_t __err_rc = (x); if (__err_rc != ESP_OK) { (void)fprintf(stderr, "[shim] ESP_ERROR_CHECK failed: %d\n", (int)__err_rc); } } while (0)
