#include "merge_engine.h"
#include "dmx_buffer.h"
#include "sender_tracker.h"
#include "stats.h"
#include "config_schema.h"
#include "frame_router.h"
#include "scene_engine.h"
#include "alert.h"
#include <Arduino.h>
#include <string.h>

static inline uint32_t portFailsafeMs(const DmxOutput& out) {
    return out.failsafeTimeout > 0 ? (uint32_t)out.failsafeTimeout * 1000 : SOURCE_TIMEOUT_MS;
}

void mergeOutput(int outIdx) {
    const DmxOutput& out = cfg.outputs[outIdx];
    uint32_t now = millis();
    uint32_t fto = portFailsafeMs(out);

    int     contrib[MAX_SENDERS], nc = 0;
    uint8_t topPrio = 0;
    for (int i = 0; i < MAX_SENDERS; i++) {
        const Sender& s = senders[i];
        if (s.ip == 0 || (uint16_t)s.universe != portAddress(out)) continue;
        if (now - s.lastMs >= fto) continue;
        contrib[nc++] = i;
        if (s.priority > topPrio) topPrio = s.priority;
    }
    if (nc == 0) {
        if (!outSrcLost[outIdx]) rxLossCount[outIdx]++;
        outSrcLost[outIdx] = true;
        // Webhook alert on source loss
        const char* ip = (nc > 0 && contrib[0] < MAX_SENDERS) ? nullptr : nullptr;
        alertSourceLost(outIdx, ip);
        switch (out.lossMode) {
            case LOSS_ZERO:
                dmxBufWriteBegin(outIdx);
                memset(&dmxBuffers[outIdx].data[1], 0, 512);
                dmxBufWriteEnd(outIdx);
                break;
            case LOSS_STOP:
                // Stop transmitting — handled by txStyle = 0 frame or no-frame path
                break;
            case LOSS_PRESET:
                sceneRecall(out.lossPreset, 0, outIdx);
                break;
            case LOSS_HOME:
                sceneRecallHome(outIdx);
                break;
            case LOSS_HOLD:
            default:
                // Hold last — do nothing, the buffer retains its last written value
                break;
        }
        return;
    }
    outSrcLost[outIdx] = false;
    alertSourceRestored(outIdx);

    // Scene priority override: if an active scene has higher priority than all
    // live network sources, keep the scene's DMX data (already written to
    // dmxBuffers by the scene engine) and skip the network merge for this tick.
    uint8_t scenePrio = sceneActivePriority(outIdx);
    if (scenePrio > topPrio && scenePrio > 0) {
        return;
    }

    // MERGE_LTP_TAKEOVER: highest-priority source wins; if tie, newest wins
    if (out.mergeMode == MERGE_LTP_TAKEOVER && nc > 0) {
        int best = -1;
        for (int k = 0; k < nc; k++) {
            const Sender& s = senders[contrib[k]];
            if (best < 0) { best = k; continue; }
            const Sender& b = senders[contrib[best]];
            if (s.priority > b.priority || s.lastMs > b.lastMs) best = k;
        }
        dmxBufWriteBegin(outIdx);
        memcpy(&dmxBuffers[outIdx].data[1], senders[contrib[best]].data, senders[contrib[best]].dataLen);
        dmxBufWriteEnd(outIdx);
        return;
    }

    // MERGE_PRIORITY: take highest-priority source(s), per-channel max
    if (out.mergeMode == MERGE_PRIORITY && nc > 0) {
        uint8_t merged[256];
        memset(merged, 0, sizeof(merged));
        for (int k = 0; k < nc; k++) {
            const Sender& s = senders[contrib[k]];
            if (s.priority < topPrio) continue;
            int len = s.dataLen < 256 ? s.dataLen : 256;
            for (int c = 0; c < len; c++)
                if (s.data[c] > merged[c]) merged[c] = s.data[c];
        }
        dmxBufWriteBegin(outIdx);
        memcpy(&dmxBuffers[outIdx].data[1], merged, 256);
        dmxBufWriteEnd(outIdx);
        return;
    }

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
        uint32_t fto = portFailsafeMs(cfg.outputs[i]);
        for (int s = 0; s < MAX_SENDERS; s++) {
            if (senders[s].ip == 0 || (uint16_t)senders[s].universe != portAddress(cfg.outputs[i])) continue;
            if (now - senders[s].lastMs >= fto && !outSrcLost[i]) {
                mergeOutput(i);
                break;
            }
        }
    }
}
