#pragma once

#include "esp_log.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Log levels
typedef enum {
    LOG_LEVEL_ERROR = ESP_LOG_ERROR,
    LOG_LEVEL_WARN = ESP_LOG_WARN,
    LOG_LEVEL_INFO = ESP_LOG_INFO,
    LOG_LEVEL_DEBUG = ESP_LOG_DEBUG,
    LOG_LEVEL_VERBOSE = ESP_LOG_VERBOSE
} log_level_t;

// Ring buffer capacity (must match spec 13/23)
#define LOG_RING_CAPACITY 32

// Single log entry in the ring buffer
typedef struct {
    uint32_t timestamp_ms;
    log_level_t level;
    char tag[16];
    char message[128];
} log_entry_t;

// Logger interface
void logger_init(void);
void logger_set_level(log_level_t level);
void logger_log(log_level_t level, const char* tag, const char* format, ...);

// Ring buffer accessors
const log_entry_t* logger_get_ring_buffer(size_t* count);

// Log macros
#define LOG_ERROR(tag, format, ...) logger_log(LOG_LEVEL_ERROR, tag, format, ##__VA_ARGS__)
#define LOG_WARN(tag, format, ...)  logger_log(LOG_LEVEL_WARN, tag, format, ##__VA_ARGS__)
#define LOG_INFO(tag, format, ...)  logger_log(LOG_LEVEL_INFO, tag, format, ##__VA_ARGS__)
#define LOG_DEBUG(tag, format, ...) logger_log(LOG_LEVEL_DEBUG, tag, format, ##__VA_ARGS__)
#define LOG_VERBOSE(tag, format, ...) logger_log(LOG_LEVEL_VERBOSE, tag, format, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif
