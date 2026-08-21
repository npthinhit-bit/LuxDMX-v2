#include "syslog_client.h"
#include "logger.h"
#include "esp_timer.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "syslog";
static int s_socket = -1;
static bool s_enabled = false;
static uint8_t s_facility = 1;
static char s_hostname[32] = "luxdmx";
static struct sockaddr_in s_destination;

void syslogInit(bool enabled, const char *server, uint16_t port, uint8_t facility,
                const char *hostname)
{
    s_enabled = enabled;
    s_facility = facility > 23u ? 23u : facility;
    if (hostname != NULL && hostname[0] != '\0') {
        strncpy(s_hostname, hostname, sizeof(s_hostname) - 1u);
        s_hostname[sizeof(s_hostname) - 1u] = '\0';
    }
    if (!enabled || server == NULL || server[0] == '\0') return;

    memset(&s_destination, 0, sizeof(s_destination));
    s_destination.sin_family = AF_INET;
    s_destination.sin_port = htons(port == 0 ? 514 : port);
    s_destination.sin_addr.s_addr = inet_addr(server);
    if (s_destination.sin_addr.s_addr == INADDR_NONE) return;
    s_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
}

void syslogSend(syslog_level_t level, const char *message)
{
    if (message == NULL) return;
    const unsigned severity = level > SYSLOG_DEBUG ? SYSLOG_DEBUG : (unsigned)level;
    LOG_INFO(TAG, "<%u> %s", s_facility * 8u + severity, message);
    if (!s_enabled || s_socket < 0) return;

    char datagram[512];
    const uint64_t uptime_ms = esp_timer_get_time() / 1000ULL;
    int length = snprintf(datagram, sizeof(datagram), "<%u>1 %llu %s LuxDMX - - - %s",
                          s_facility * 8u + severity,
                          (unsigned long long)uptime_ms, s_hostname, message);
    if (length <= 0) return;
    if ((size_t)length >= sizeof(datagram)) length = (int)sizeof(datagram) - 1;
    (void)sendto(s_socket, datagram, (size_t)length, 0,
                 (const struct sockaddr *)&s_destination, sizeof(s_destination));
}

void syslogPrintf(syslog_level_t level, const char *format, ...)
{
    if (format == NULL) return;
    char message[384];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    syslogSend(level, message);
}
