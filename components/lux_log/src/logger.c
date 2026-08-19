#include "logger.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static log_level_t current_level = LOG_LEVEL_INFO;

// 32-entry ring buffer (spec 13/23)
static log_entry_t log_ring[LOG_RING_CAPACITY];
static volatile size_t log_ring_head = 0;
static volatile size_t log_ring_count = 0;
static volatile size_t log_ring_wrap = 0; // total entries ever written (for wrap detection)

void logger_init(void) {
    // Initialize ESP-IDF logging
    esp_log_level_set("*", ESP_LOG_INFO);
    memset(log_ring, 0, sizeof(log_ring));
    log_ring_head = 0;
    log_ring_count = 0;
    log_ring_wrap = 0;
}

void logger_set_level(log_level_t level) {
    current_level = level;
    esp_log_level_set("*", (esp_log_level_t)level);
}

void logger_log(log_level_t level, const char* tag, const char* format, ...) {
    if (level > current_level) {
        return;
    }

    // Write to the ring buffer
    log_entry_t* entry = &log_ring[log_ring_head];
    entry->timestamp_ms = (uint32_t)esp_log_timestamp();
    entry->level = level;

    // Copy tag (truncate if too long)
    size_t tag_len = strlen(tag);
    if (tag_len >= sizeof(entry->tag)) {
        tag_len = sizeof(entry->tag) - 1;
    }
    memcpy(entry->tag, tag, tag_len);
    entry->tag[tag_len] = '\0';

    // Format the message
    va_list args;
    va_start(args, format);
    vsnprintf(entry->message, sizeof(entry->message), format, args);
    va_end(args);

    // Advance head
    log_ring_head = (log_ring_head + 1) % LOG_RING_CAPACITY;
    if (log_ring_count < LOG_RING_CAPACITY) {
        log_ring_count++;
    } else {
        log_ring_wrap++;
    }

    // Also print to serial (ESP_LOG style)
    va_start(args, format);
    char prefix[32];
    const char* level_str = "I";
    switch (level) {
        case LOG_LEVEL_ERROR:   level_str = "E"; break;
        case LOG_LEVEL_WARN:    level_str = "W"; break;
        case LOG_LEVEL_INFO:    level_str = "I"; break;
        case LOG_LEVEL_DEBUG:   level_str = "D"; break;
        case LOG_LEVEL_VERBOSE: level_str = "V"; break;
    }
    int prefix_len = snprintf(prefix, sizeof(prefix), "%s (%lu) %s: ",
                              level_str, esp_log_timestamp(), tag);
    fwrite(prefix, 1, prefix_len, stdout);
    vfprintf(stdout, format, args);
    fputc('\n', stdout);
    va_end(args);
}

const log_entry_t* logger_get_ring_buffer(size_t* count) {
    if (count) {
        *count = log_ring_count;
    }
    // Return pointer at the oldest entry (head - count)
    if (log_ring_count == 0) {
        return NULL;
    }
    size_t start = (log_ring_head + LOG_RING_CAPACITY - log_ring_count) % LOG_RING_CAPACITY;
    return &log_ring[start];
}
