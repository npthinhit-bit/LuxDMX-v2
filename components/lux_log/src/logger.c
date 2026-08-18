#include "logger.h"
#include <stdarg.h>
#include <stdio.h>

static log_level_t current_level = LOG_LEVEL_INFO;

void logger_init(void) {
    // Initialize ESP-IDF logging
    esp_log_level_set("*", ESP_LOG_INFO);
}

void logger_set_level(log_level_t level) {
    current_level = level;
    esp_log_level_set("*", (esp_log_level_t)level);
}

void logger_log(log_level_t level, const char* tag, const char* format, ...) {
    if (level > current_level) {
        return;
    }

    va_list args;
    va_start(args, format);

    switch (level) {
        case LOG_LEVEL_ERROR:
            printf("E (%lu) %s: ", esp_log_timestamp(), tag);
            vprintf(format, args);
            printf("\n");
            break;
        case LOG_LEVEL_WARN:
            printf("W (%lu) %s: ", esp_log_timestamp(), tag);
            vprintf(format, args);
            printf("\n");
            break;
        case LOG_LEVEL_INFO:
            printf("I (%lu) %s: ", esp_log_timestamp(), tag);
            vprintf(format, args);
            printf("\n");
            break;
        case LOG_LEVEL_DEBUG:
            printf("D (%lu) %s: ", esp_log_timestamp(), tag);
            vprintf(format, args);
            printf("\n");
            break;
        case LOG_LEVEL_VERBOSE:
            printf("V (%lu) %s: ", esp_log_timestamp(), tag);
            vprintf(format, args);
            printf("\n");
            break;
    }

    va_end(args);
}