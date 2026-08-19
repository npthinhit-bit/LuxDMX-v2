/*
 * Native test shim - header
 * Use this header to include all shim headers in one go
 */
#pragma once

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_chip_info.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_rom_sys.h"

/* ---- Test control functions ---- */

/**
 * Force GPIO0 to read as LOW (simulates button press for portal mode)
 */
void test_shim_set_gpio0_low(bool low);

/**
 * Set the simulated chip model (CHIP_ESP32, CHIP_ESP32_S3, etc.)
 */
void test_shim_set_chip_model(int model);

/**
 * Enable/disable PSRAM for testing
 */
void test_shim_set_psram_enabled(bool enabled);

/**
 * Set mock WiFi scan results
 */
void test_shim_set_scan_networks(const char** networks, int count);

/**
 * Reset the NVS in-memory store (clears all saved values)
 */
void test_shim_reset_nvs(void);
