#include "stats.h"
#include "config_schema.h"
#include <Arduino.h>

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

volatile uint8_t g_srcStatus = 0;  // SRC_NORMAL

volatile uint32_t g_rdmSent = 0;
volatile uint32_t g_rdmRecv = 0;
int rdmCount = 0;

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
