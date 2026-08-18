#pragma once

#include "esp_log.h"

// Log levels
typedef enum {
    LOG_LEVEL_ERROR = ESP_LOG_ERROR,
    LOG_LEVEL_WARN = ESP_LOG_WARN,
    LOG_LEVEL_INFO = ESP_LOG_INFO,
    LOG_LEVEL_DEBUG = ESP_LOG_DEBUG,
    LOG_LEVEL_VERBOSE = ESP_LOG_VERBOSE
} log_level_t;

// Logger interface
void logger_init(void);
void logger_set_level(log_level_t level);
void logger_log(log_level_t level, const char* tag, const char* format, ...);

// Log macros
#define LOG_ERROR(tag, format, ...) logger_log(LOG_LEVEL_ERROR, tag, format, ##__VA_ARGS__)
#define LOG_WARN(tag, format, ...)  logger_log(LOG_LEVEL_WARN, tag, format, ##__VA_ARGS__)
#define LOG_INFO(tag, format, ...)  logger_log(LOG_LEVEL_INFO, tag, format, ##__VA_ARGS__)
#define LOG_DEBUG(tag, format, ...) logger_log(LOG_LEVEL_DEBUG, tag, format, ##__VA_ARGS__)
#define LOG_VERBOSE(tag, format, ...) logger_log(LOG_LEVEL_VERBOSE, tag, format, ##__VA_ARGS__)