#include "stats.h"
#include <Arduino.h>
#include <string.h>

#ifndef CONFIG_LUXDMX_LOG_BUF_CAP
#define CONFIG_LUXDMX_LOG_BUF_CAP 32
#endif
#define LOG_BUF_CAP CONFIG_LUXDMX_LOG_BUF_CAP
#define LOG_FRAME_LEN 8

static StatsState g_stats;
StatsState&       stats()
{
    return g_stats;
}

void maybeLog(int outIdx, const uint8_t* cur, uint16_t len, uint32_t ip, uint8_t proto)
{
    uint32_t now = millis();
    if (now - stats().logLastMs < 100)
        return;
    stats().logLastMs = now;
    uint8_t idx       = stats().logHead;
    if (stats().logCount < LOG_BUF_CAP)
        stats().logCount++;
    stats().logHead           = (stats().logHead + 1) % LOG_BUF_CAP;
    stats().logBuf[idx].ip    = ip;
    stats().logBuf[idx].proto = proto;
    uint16_t n                = len < LOG_FRAME_LEN ? len : LOG_FRAME_LEN;
    memcpy(stats().logBuf[idx].data, cur, n);
    memset(&stats().logBuf[idx].data[n], 0, LOG_FRAME_LEN - n);
}
