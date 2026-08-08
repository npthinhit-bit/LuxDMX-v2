#include "stats.h"
#include "config_schema.h"
#include <Arduino.h>
#include <string.h>

#define LOG_BUF_CAP 32
#define LOG_FRAME_LEN 8

static struct {
    uint32_t ip;
    uint8_t  proto;
    uint8_t  data[LOG_FRAME_LEN];
} logBuf[LOG_BUF_CAP];
static uint8_t logHead = 0;
static uint8_t logCount = 0;
static uint32_t logLastMs = 0;

uint32_t frameCount    = 0;
uint32_t lastFrameMs   = 0;
float    fps           = 0.0f;

uint32_t inFrameCnt[MAX_OUTPUTS]    = {0};
uint32_t inWinMs[MAX_OUTPUTS]       = {0};
float    inFpsOut[MAX_OUTPUTS]      = {0.0f};
uint32_t outFrameCount[MAX_OUTPUTS] = {0};
uint32_t outLastFrameMs[MAX_OUTPUTS] = {0};
uint32_t outLastDmxMs[MAX_OUTPUTS]   = {0};
volatile uint32_t outInSeq[MAX_OUTPUTS] = {0};
float    outFps[MAX_OUTPUTS]         = {0.0f};
volatile uint32_t txFrames[MAX_OUTPUTS] = {0};
float    outTxFps[MAX_OUTPUTS]       = {0.0f};
bool outSrcLost[MAX_OUTPUTS]     = {true, true, true, true};
bool manualMode = false;

float    jitterMs     = 0.0f;
uint32_t prevFrameMs  = 0;
uint32_t startMs      = 0;
uint32_t lastDmxMs    = 0;
uint32_t lastWsPush   = 0;

volatile uint8_t g_srcStatus = 0;

volatile uint32_t g_rdmSent = 0;
volatile uint32_t g_rdmRecv = 0;
int rdmCount = 0;
rdm_uid_t rdmTod[RDM_TOD_MAX] = {};

float outFpsLive(int i) {
    if (!outTxFps[i] && !outFps[i]) return 0.0f;
    return outTxFps[i] > 0.0f ? outTxFps[i] : outFps[i];
}

float inFpsLive(int i) {
    return inFpsOut[i];
}

uint32_t uptimeSec() {
    return (millis() - startMs) / 1000;
}

void maybeLog(int outIdx, const uint8_t* cur, uint16_t len, uint32_t ip, uint8_t proto) {
    uint32_t now = millis();
    if (now - logLastMs < 100) return;
    logLastMs = now;
    uint8_t idx = logHead;
    if (logCount < LOG_BUF_CAP) logCount++;
    logHead = (logHead + 1) % LOG_BUF_CAP;
    logBuf[idx].ip = ip;
    logBuf[idx].proto = proto;
    uint16_t n = len < LOG_FRAME_LEN ? len : LOG_FRAME_LEN;
    memcpy(logBuf[idx].data, cur, n);
    memset(&logBuf[idx].data[n], 0, LOG_FRAME_LEN - n);
}
