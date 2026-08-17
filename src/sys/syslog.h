#pragma once
#include "config_schema.h"
#include <Arduino.h>

// Syslog level codes (RFC 5424 severity).
enum SyslogLevel
{
    SYSLOG_EMERG  = 0,
    SYSLOG_ALERT  = 1,
    SYSLOG_CRIT   = 2,
    SYSLOG_ERR    = 3,
    SYSLOG_WARN   = 4,
    SYSLOG_NOTICE = 5,
    SYSLOG_INFO   = 6,
    SYSLOG_DEBUG  = 7,
};

void syslogInit();
void syslogSend(SyslogLevel level, const char* msg);
void syslogPrintf(SyslogLevel level, const char* fmt, ...);
