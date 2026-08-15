#pragma once
#include <stdint.h>
#include "rdm_types.h"
#include "config_schema.h"

#ifndef CONFIG_LUXDMX_LOG_BUF_CAP
#define CONFIG_LUXDMX_LOG_BUF_CAP 32
#endif
#define LOG_BUF_CAP CONFIG_LUXDMX_LOG_BUF_CAP
#define LOG_FRAME_LEN 8

enum { SRC_NORMAL = 0, SRC_CONFLICT = 1, SRC_MERGING = 2 };

#ifndef CONFIG_LUXDMX_RDM_TOD_MAX
#define CONFIG_LUXDMX_RDM_TOD_MAX 64
#endif
#define RDM_TOD_MAX CONFIG_LUXDMX_RDM_TOD_MAX

struct StatsState {
    uint32_t frameCount    = 0;
    uint32_t lastFrameMs   = 0;
    float    fps           = 0.0f;
    uint32_t inFrameCnt[MAX_OUTPUTS]    = {};
    uint32_t inWinMs[MAX_OUTPUTS]       = {};
    float    inFpsOut[MAX_OUTPUTS]      = {};
    uint32_t outFrameCount[MAX_OUTPUTS] = {};
    uint32_t outLastFrameMs[MAX_OUTPUTS] = {};
    uint32_t outLastDmxMs[MAX_OUTPUTS]   = {};
    volatile uint32_t outInSeq[MAX_OUTPUTS] = {};
    float    outFps[MAX_OUTPUTS]         = {};
    volatile uint32_t txFrames[MAX_OUTPUTS] = {};
    float    outTxFps[MAX_OUTPUTS]       = {};
    bool     outSrcLost[MAX_OUTPUTS]     = {true, true, true, true};
    uint32_t rxFrameCount[MAX_OUTPUTS] = {};
    uint32_t rxLossCount[MAX_OUTPUTS]  = {};
    bool     manualMode = false;
    float    jitterMs     = 0.0f;
    uint32_t prevFrameMs  = 0;
    uint32_t startMs      = 0;
    uint32_t lastDmxMs    = 0;
    uint32_t lastWsPush   = 0;
    volatile uint8_t srcStatus = 0;
    volatile uint32_t rdmSent = 0;
    volatile uint32_t rdmRecv = 0;
    int      rdmCount = 0;
    rdm_uid_t rdmTod[RDM_TOD_MAX] = {};

    struct LogEntry { uint32_t ip; uint8_t proto; uint8_t data[LOG_FRAME_LEN]; };
    LogEntry logBuf[LOG_BUF_CAP] = {};
    uint8_t  logHead = 0;
    uint8_t  logCount = 0;
    uint32_t logLastMs = 0;
};

StatsState& stats();

inline float outFpsLive(int i) {
    if (!stats().outTxFps[i] && !stats().outFps[i]) return 0.0f;
    return stats().outTxFps[i] > 0.0f ? stats().outTxFps[i] : stats().outFps[i];
}

inline float inFpsLive(int i) {
    return stats().inFpsOut[i];
}

inline uint32_t uptimeSec() {
    return (millis() - stats().startMs) / 1000;
}

void maybeLog(int outIdx, const uint8_t* cur, uint16_t len, uint32_t ip, uint8_t proto);
