// Syslog client — forwards log messages to a remote UDP syslog server (RFC 5424).
// Also echoes to local Serial for debugging. Called from system events.
#include "syslog.h"
#include "config_schema.h"
#ifdef ESP32
#include <WiFi.h>
#include <WiFiUdp.h>
#include <lwip/sockets.h>
#endif
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

static int g_syslogSock = -1;

void syslogInit()
{
    if (!cfg.syslogEnabled || cfg.syslogServer.length() == 0)
        return;
#ifdef ESP32
    g_syslogSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#endif
}

void syslogSend(SyslogLevel level, const char* msg)
{
    // Echo to debug output
#ifdef ESP32
    Serial.printf("[SYSLOG %d] %s\n", (int)level, msg);
#else
    printf("[SYSLOG %d] %s\n", (int)level, msg);
#endif

    if (!cfg.syslogEnabled || cfg.syslogServer.length() == 0)
        return;
#ifdef ESP32
    if (g_syslogSock < 0)
        return;
    IPAddress ip;
    if (!ip.fromString(cfg.syslogServer))
        return;

    // RFC 5424 message format: <PRI>1 TIMESTAMP HOSTNAME APP PID MSGID MSG
    int  pri = (cfg.syslogFacility * 8) + (int)level;
    char line[576];
    snprintf(line, sizeof(line), "<%d>1 %lu %s LuxDMX - - %s", pri, millis(), cfg.hostname.c_str(), msg);

    struct sockaddr_in dst = {};
    dst.sin_family         = AF_INET;
    dst.sin_port           = htons(cfg.syslogPort > 0 ? cfg.syslogPort : 514);
    dst.sin_addr.s_addr    = ip;
    sendto(g_syslogSock, line, strlen(line), 0, (struct sockaddr*)&dst, sizeof(dst));
#endif
}

void syslogPrintf(SyslogLevel level, const char* fmt, ...)
{
    char    buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    syslogSend(level, buf);
}
