#include "merge_engine.h"
#include "dmx_buffer.h"
#include "sender_tracker.h"
#include "stats.h"
#include "config_schema.h"
#include "frame_router.h"
#include <Arduino.h>
#include <string.h>

void mergeOutput(int outIdx) {
    const DmxOutput& out = cfg.outputs[outIdx];
    uint32_t now = millis();

    int     contrib[MAX_SENDERS], nc = 0;
    uint8_t topPrio = 0;
    for (int i = 0; i < MAX_SENDERS; i++) {
        const Sender& s = senders[i];
        if (s.ip == 0 || (uint16_t)s.universe != portAddress(out)) continue;
        if (now - s.lastMs >= SOURCE_TIMEOUT_MS) continue;
        contrib[nc++] = i;
        if (s.priority > topPrio) topPrio = s.priority;
    }
    if (nc == 0) {
        outSrcLost[outIdx] = true;
        if (out.lossMode == LOSS_ZERO) {
            dmxBufWriteBegin(outIdx);
            memset(&dmxBuffers[outIdx].data[1], 0, 512);
            dmxBufWriteEnd(outIdx);
        }
        return;
    }
    outSrcLost[outIdx] = false;

    if (out.mergeMode == MERGE_HTP && nc > 1) {
        uint8_t merged[512];
        memset(merged, 0, sizeof(merged));
        for (int k = 0; k < nc; k++) {
            const Sender& s = senders[contrib[k]];
            if (s.priority < topPrio) continue;
            for (int c = 0; c < s.dataLen; c++)
                if (s.data[c] > merged[c]) merged[c] = s.data[c];
        }
        dmxBufWriteBegin(outIdx);
        memcpy(&dmxBuffers[outIdx].data[1], merged, 512);
        dmxBufWriteEnd(outIdx);
        return;
    }

    bool usePrio = (out.mergeMode != MERGE_OFF);
    int newest = -1; uint32_t newestMs = 0;
    for (int k = 0; k < nc; k++) {
        const Sender& s = senders[contrib[k]];
        if (usePrio && s.priority < topPrio) continue;
        if (newest < 0 || s.lastMs >= newestMs) { newest = contrib[k]; newestMs = s.lastMs; }
    }
    if (newest >= 0) {
        dmxBufWriteBegin(outIdx);
        memcpy(&dmxBuffers[outIdx].data[1], senders[newest].data, senders[newest].dataLen);
        dmxBufWriteEnd(outIdx);
    }
}

void mergeOutputTimed() {
    uint32_t now = millis();
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (!cfg.outputs[i].enabled) continue;
        for (int s = 0; s < MAX_SENDERS; s++) {
            if (senders[s].ip == 0 || (uint16_t)senders[s].universe != portAddress(cfg.outputs[i])) continue;
            if (now - senders[s].lastMs >= SOURCE_TIMEOUT_MS && !outSrcLost[i]) {
                mergeOutput(i);
                break;
            }
        }
    }
}
