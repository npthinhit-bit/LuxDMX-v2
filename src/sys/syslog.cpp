// Syslog client — forwards log messages to a remote UDP syslog server (RFC 5424).
// Also echoes to local Serial for debugging. Called from system events.
#include "syslog.h"
#include "config_schema.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <stdarg.h>
#include <stdio.h>

static WiFiUDP g_syslogUdp;

void syslogInit() {
    if (!cfg.syslogEnabled || cfg.syslogServer.length() == 0) return;
    // Parse syslog server IP and port
    IPAddress ip;
    if (!ip.fromString(cfg.syslogServer)) return;
    g_syslogUdp.begin(0);  // ephemeral local port
}

void syslogSend(SyslogLevel level, const char* msg) {
    // Echo to Serial
    Serial.printf("[SYSLOG %d] %s\n", (int)level, msg);

    if (!cfg.syslogEnabled || cfg.syslogServer.length() == 0) return;
    IPAddress ip;
    if (!ip.fromString(cfg.syslogServer)) return;

    // RFC 5424 message format: <PRI>1 TIMESTAMP HOSTNAME APP PID MSGID MSG
    // PRI = facility * 8 + severity
    int pri = (cfg.syslogFacility * 8) + (int)level;
    char line[576];
    snprintf(line, sizeof(line), "<%d>1 %lu %s LuxDMX - - %s",
             pri, millis(), cfg.hostname.c_str(), msg);
    g_syslogUdp.beginPacket(ip, cfg.syslogPort > 0 ? cfg.syslogPort : 514);
    g_syslogUdp.write((uint8_t*)line, strlen(line));
    g_syslogUdp.endPacket();
}

void syslogPrintf(SyslogLevel level, const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    syslogSend(level, buf);
}
