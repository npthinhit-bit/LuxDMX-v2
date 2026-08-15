#include "ws_frame.h"
#include "stats.h"
#include "dmx_buffer.h"
#include "merge_engine.h"
#include "sender_tracker.h"
#include "config_schema.h"
#include "output_init.h"   // dmxIsDelta, TXSRC_ARTNET
#include "net_state.h"   // g_apMode, g_useEth, WiFi
#include <Arduino.h>
#include <ETH.h>

uint8_t  wsBuf[WS_FRAME_LEN];
uint8_t  wsChangedBitmap = 0;
uint32_t wsFrameSeq = 0;
uint16_t wsClientSub[WS_MAX_CLIENTS] = {0};

// Last-sent DMX per output, for delta detection.
// Only 512 slots (data[1..512]) are compared, NOT the start code at data[0].
static uint8_t wsLastDmx[4][WS_CHANS_PER_OUT] = {0};

void wsBuildFrame() {
    wsFrameSeq++;
    wsChangedBitmap = 0;

    // --- Header (unchanged: fps/rssi/heap/uptime/senders/srcStatus/jitter) ---
    uint16_t fpsI  = (uint16_t)(stats().fps * 10.0f);
    int16_t  rssi;
    if (g_apMode)      rssi = 1;
    else if (g_useEth) { int s = ETH.linkSpeed(); rssi = (int16_t)(s >= 10 ? s : 100); }
    else               rssi = (int16_t)WiFi.RSSI();
    uint32_t heap  = ESP.getFreeHeap();
    uint32_t upS   = uptimeSec();
    uint16_t jitI  = (uint16_t)(stats().jitterMs * 10.0f < 65535.0f ? stats().jitterMs * 10.0f : 65535.0f);

    wsBuf[0]  = fpsI >> 8;                       wsBuf[1]  = fpsI & 0xFF;
    wsBuf[2]  = (uint8_t)((uint16_t)rssi >> 8);  wsBuf[3]  = rssi & 0xFF;
    wsBuf[4]  = heap >> 24;  wsBuf[5]  = (heap>>16)&0xFF;
    wsBuf[6]  = (heap>>8)&0xFF; wsBuf[7] = heap & 0xFF;
    wsBuf[8]  = upS >> 24;   wsBuf[9]  = (upS>>16)&0xFF;
    wsBuf[10] = (upS>>8)&0xFF; wsBuf[11] = upS & 0xFF;
    wsBuf[12] = activeSenderCount();
    wsBuf[13] = stats().srcStatus;
    wsBuf[14] = jitI >> 8;  wsBuf[15] = jitI & 0xFF;

    // --- DMX data: compare to last-sent for delta, copy into frame ---
    // DMX data stays at offset 16 (unchanged for frontend compatibility).
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        const uint8_t* cur = &dmxBufferState().buffers[i].data[1];   // skip start code at data[0]
        bool changed = false;
        for (int j = 0; j < WS_CHANS_PER_OUT; j++) {
            if (cur[j] != wsLastDmx[i][j]) { changed = true; break; }
        }
        if (changed) wsChangedBitmap |= (uint8_t)(1 << i);
        memcpy(wsLastDmx[i], cur, WS_CHANS_PER_OUT);
        memcpy(&wsBuf[WS_HEADER_LEN + i * WS_CHANS_PER_OUT], cur, WS_CHANS_PER_OUT);
    }

    // --- Changed-universe bitmap (before nav tail, invisible to frontend) ---
    wsBuf[WS_CHANGED_OFF] = wsChangedBitmap;

    // --- Per-output stats (unchanged offset: WS_HEADER_LEN + WS_CHANS_ALL) ---
    const int OUT_FPS_OFF = WS_HEADER_LEN + WS_CHANS_ALL;  // 2064
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        uint16_t f = (uint16_t)(outFpsLive(i) * 10.0f);
        wsBuf[OUT_FPS_OFF + 2 * i]     = f >> 8;
        wsBuf[OUT_FPS_OFF + 2 * i + 1] = f & 0xFF;
    }
    uint32_t nowMs = millis();
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (nowMs - stats().inWinMs[i] >= 1000) {
            stats().inFpsOut[i] = (float)stats().inFrameCnt[i] * 1000.0f / (float)(nowMs - stats().inWinMs[i]);
            stats().inFrameCnt[i] = 0; stats().inWinMs[i] = nowMs;
        }
        uint16_t inI = (uint16_t)(stats().inFpsOut[i] * 10.0f < 65535.0f ? stats().inFpsOut[i] * 10.0f : 65535.0f);
        wsBuf[OUT_FPS_OFF + 2 * MAX_OUTPUTS + 2 * i]     = inI >> 8;
        wsBuf[OUT_FPS_OFF + 2 * MAX_OUTPUTS + 2 * i + 1] = inI & 0xFF;
    }

    // TX style bits
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        uint8_t st = 0;
        if (dmxIsDelta(i))                            st |= 0x01;
        if (cfg.outputs[i].txStyleSrc == TXSRC_ARTNET) st |= 0x02;
        wsBuf[OUT_FPS_OFF + 4 * MAX_OUTPUTS + i] = st;
    }

    // --- Nav tail: fixtures(2) rdmTx(4) rdmRx(4) — offset shifted by 1 for bitmap ---
    int t = WS_CHANGED_OFF + 1;   // 2085
    uint16_t nf = (uint16_t)stats().rdmCount;
    uint32_t rtx = stats().rdmSent, rrx = stats().rdmRecv;
    wsBuf[t]   = nf >> 8;   wsBuf[t+1] = nf & 0xFF;
    wsBuf[t+2] = rtx >> 24; wsBuf[t+3] = (rtx>>16)&0xFF;
    wsBuf[t+4] = (rtx>>8)&0xFF; wsBuf[t+5] = rtx & 0xFF;
    wsBuf[t+6] = rrx >> 24; wsBuf[t+7] = (rrx>>16)&0xFF;
    wsBuf[t+8] = (rrx>>8)&0xFF; wsBuf[t+9] = rrx & 0xFF;
}
