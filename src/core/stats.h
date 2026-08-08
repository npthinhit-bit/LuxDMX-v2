#pragma once
#include <stdint.h>

enum { SRC_NORMAL = 0, SRC_CONFLICT = 1, SRC_MERGING = 2 };

// Cross-cutting runtime statistics shared across core/net/app layers.
// Declared here (extern), defined in stats.cpp. Firmware build only — host tests
// don't use these.

extern uint32_t frameCount;
extern uint32_t lastFrameMs;
extern float    fps;

extern uint32_t inFrameCnt[];
extern uint32_t inWinMs[];
extern float    inFpsOut[];
extern uint32_t outFrameCount[];
extern uint32_t outLastFrameMs[];
extern uint32_t outLastDmxMs[];
extern volatile uint32_t outInSeq[];
extern float    outFps[];
extern volatile uint32_t txFrames[];
extern float    outTxFps[];
extern bool     outSrcLost[];

extern float    jitterMs;
extern uint32_t prevFrameMs;
extern uint32_t startMs;
extern uint32_t lastDmxMs;
extern uint32_t lastWsPush;

extern volatile uint8_t g_srcStatus;
extern bool             manualMode;

extern volatile uint32_t g_rdmSent;
extern volatile uint32_t g_rdmRecv;
extern int rdmCount;

float outFpsLive(int i);
float inFpsLive(int i);
uint32_t uptimeSec();

void maybeLog(int outIdx, const uint8_t* cur, uint16_t len, uint32_t ip, uint8_t proto);
