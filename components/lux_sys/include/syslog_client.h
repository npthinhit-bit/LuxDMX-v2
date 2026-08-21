#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYSLOG_EMERGENCY = 0,
    SYSLOG_ALERT = 1,
    SYSLOG_CRITICAL = 2,
    SYSLOG_ERROR = 3,
    SYSLOG_WARNING = 4,
    SYSLOG_NOTICE = 5,
    SYSLOG_INFO = 6,
    SYSLOG_DEBUG = 7,
} syslog_level_t;

void syslogInit(bool enabled, const char *server, uint16_t port, uint8_t facility,
                const char *hostname);
void syslogSend(syslog_level_t level, const char *message);
void syslogPrintf(syslog_level_t level, const char *format, ...);

#ifdef __cplusplus
}
#endif
