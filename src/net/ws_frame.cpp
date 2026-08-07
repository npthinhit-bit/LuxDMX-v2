#include "ws_frame.h"
#include "stats.h"
#include "dmx_buffer.h"
#include "sender_tracker.h"
#include "config_schema.h"
#include "output_init.h"   // dmxIsDelta, TXSRC_ARTNET
#include "network.h"       // g_apMode, g_useEth, WiFi
#include <Arduino.h>
#include <ETH.h>

uint8_t wsBuf[WS_FRAME_LEN];

void wsBuildFrame() {
    // Header: fps(2) rssi(2) heap(4) uptime(4) senders(1) srcStatus(1) jitter(2)
    uint16_t fpsI  = (uint16_t)(fps * 10.0f);
    int16_t  rssi;
    if (g_apMode)      rssi = 1;
    else if (g_useEth) { int s = ETH.linkSpeed(); rssi = (int16_t)(s >= 10 ? s : 100); }
    else               rssi = (int16_t)WiFi.RSSI();
    uint32_t heap  = ESP.getFreeHeap();
    uint32_t upS   = uptimeSec();
    uint16_t jitI  = (uint16_t)(jitterMs * 10.0f < 65535.0f ? jitterMs * 10.0f : 65535.0f);

    wsBuf[0]  = fpsI >> 8;                       wsBuf[1]  = fpsI & 0xFF;
    wsBuf[2]  = (uint8_t)((uint16_t)rssi >> 8);  wsBuf[3]  = rssi & 0xFF;
    wsBuf[4]  = heap >> 24;  wsBuf[5]  = (heap>>16)&0xFF;
    wsBuf[6]  = (heap>>8)&0xFF; wsBuf[7] = heap & 0xFF;
    wsBuf[8]  = upS >> 24;   wsBuf[9]  = (upS>>16)&0xFF;
    wsBuf[10] = (upS>>8)&0xFF; wsBuf[11] = upS & 0xFF;
    wsBuf[12] = activeSenderCount();
    wsBuf[13] = g_srcStatus;
    wsBuf[14] = jitI >> 8;  wsBuf[15] = jitI & 0xFF;

    // All 4 outputs' 512 channels.
    for (int i = 0; i < MAX_OUTPUTS; i++)
        memcpy(&wsBuf[16 + i * WS_CHANS_PER_OUT], &dmxBuffers[i].data[1], WS_CHANS_PER_OUT);

    // Per-output output/input FPS + TX style.
    const int OUT_FPS_OFF = 16 + WS_CHANS_ALL;
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        uint16_t f = (uint16_t)(outFpsLive(i) * 10.0f);
        wsBuf[OUT_FPS_OFF + 2 * i]     = f >> 8;
        wsBuf[OUT_FPS_OFF + 2 * i + 1] = f & 0xFF;
    }
    uint32_t nowMs = millis();
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (nowMs - inWinMs[i] >= 1000) {
            inFpsOut[i] = (float)inFrameCnt[i] * 1000.0f / (float)(nowMs - inWinMs[i]);
            inFrameCnt[i] = 0; inWinMs[i] = nowMs;
        }
        uint16_t inI = (uint16_t)(inFpsOut[i] * 10.0f < 65535.0f ? inFpsOut[i] * 10.0f : 65535.0f);
        wsBuf[OUT_FPS_OFF + 2 * MAX_OUTPUTS + 2 * i]     = inI >> 8;
        wsBuf[OUT_FPS_OFF + 2 * MAX_OUTPUTS + 2 * i + 1] = inI & 0xFF;
    }

    // TX style bits, fixed tail: fixtures(2) rdmTx(4) rdmRx(4)
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        uint8_t st = 0;
        if (dmxIsDelta(i))                            st |= 0x01;
        if (cfg.outputs[i].txStyleSrc == TXSRC_ARTNET) st |= 0x02;
        wsBuf[OUT_FPS_OFF + 4 * MAX_OUTPUTS + i] = st;
    }
    int t = 16 + WS_CHANS_ALL + WS_PEROUT_ALL;
    uint16_t nf = (uint16_t)rdmCount;
    uint32_t rtx = g_rdmSent, rrx = g_rdmRecv;
    wsBuf[t]   = nf >> 8;   wsBuf[t+1] = nf & 0xFF;
    wsBuf[t+2] = rtx >> 24; wsBuf[t+3] = (rtx>>16)&0xFF;
    wsBuf[t+4] = (rtx>>8)&0xFF; wsBuf[t+5] = rtx & 0xFF;
    wsBuf[t+6] = rrx >> 24; wsBuf[t+7] = (rrx>>16)&0xFF;
    wsBuf[t+8] = (rrx>>8)&0xFF; wsBuf[t+9] = rrx & 0xFF;
}
