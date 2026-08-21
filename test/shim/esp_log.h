/*
 * Native test shim - ESP logging
 * Minimal definitions for host-side testing without ESP-IDF
 */
#pragma once

#include <stdio.h>

#define ESP_LOG_ERROR   1
#define ESP_LOG_WARN    2
#define ESP_LOG_INFO    3
#define ESP_LOG_DEBUG   4
#define ESP_LOG_VERBOSE 5

#define esp_log_level_t int

#define ESP_LOGE(tag, fmt, ...) do { fprintf(stderr, "[E] %s: " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#define ESP_LOGW(tag, fmt, ...) do { fprintf(stderr, "[W] %s: " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#define ESP_LOGI(tag, fmt, ...) do { fprintf(stderr, "[I] %s: " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#define ESP_LOGD(tag, fmt, ...) do { fprintf(stderr, "[D] %s: " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#define ESP_LOGV(tag, fmt, ...) do { fprintf(stderr, "[V] %s: " fmt "\n", tag, ##__VA_ARGS__); } while(0)

#define esp_err_to_name(err) "ESP_ERR_UNKNOWN"

void esp_log_level_set(const char* tag, esp_log_level_t level);
unsigned long long esp_log_timestamp(void);
