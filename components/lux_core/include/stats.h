#pragma once
#include "esp_timer.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_OUTPUTS_STATS 4
#define LOG_BUF_CAP 32
#define TOD_TABLE_CAP 64

typedef struct {
    uint32_t bootMs;
    uint32_t frameCount;
    uint32_t rxFrameCount[MAX_OUTPUTS_STATS];
    uint32_t txFrameCount[MAX_OUTPUTS_STATS];
    uint32_t rxLastMs[MAX_OUTPUTS_STATS];
    uint32_t txLastMs[MAX_OUTPUTS_STATS];
    uint32_t rxLossCount[MAX_OUTPUTS_STATS];
    bool outSrcLost[MAX_OUTPUTS_STATS];
    uint8_t srcStatus;
    bool manualMode;
    uint32_t rdmSent;
    uint32_t rdmReceived;
    uint16_t rdmDevCount;
    uint8_t rdmTod[TOD_TABLE_CAP][6];
    int logHead, logTail, logCount;
    uint8_t logBuf[LOG_BUF_CAP][13];
} StatsState;

extern StatsState g_stats;

StatsState* stats(void);
void statsInit(void);
void maybeLog(int outIdx, uint32_t ip, uint8_t proto, uint16_t channel, uint8_t value);

static inline uint32_t uptimeSec(void) {
    return (uint32_t)((esp_timer_get_time() / 1000 - g_stats.bootMs) / 1000);
}
static inline float outFpsLive(int i) {
    uint32_t dt = (uint32_t)(esp_timer_get_time() / 1000 - g_stats.txLastMs[i]);
    return dt > 0 ? 1000.0f / dt : 0;
}

#ifdef __cplusplus
}
#endif