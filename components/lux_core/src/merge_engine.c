/*
 * spec 04 - Merge Engine implementation
 *
 * Contributor selection -> priority filter -> per-mode blend -> seqlock
 * commit into the live DMX buffer. Source-loss handling (zero/hold/stop/
 * preset/home) runs when no contributor is within the failsafe window.
 */
#include "merge_engine.h"
#include "sender_tracker.h"
#include "dmx_buffer.h"
#include "config_engine.h"
#include <string.h>

/* Scene engine (spec 08): instant, zero-fade recall on LOSS_PRESET / LOSS_HOME.
 * Forward-declared here; links once lux_core scene-engine sources are added. */
extern void sceneRecall(int idx, uint16_t fadeMs, int outIdx);
extern void sceneRecallHome(int outIdx);

/* Merge modes mirror cfg DmxOutput.mergemode and .losemode (config_engine.h). */
#ifndef MERGE_HTP
#define MERGE_OFF             0
#define MERGE_HTP             1
#define MERGE_LTP             2
#define MERGE_LTP_TAKEOVER    3
#define MERGE_PRIORITY        4
#define LOSS_HOLD             0
#define LOSS_ZERO             1
#define LOSS_STOP             2
#define LOSS_PRESET           3
#define LOSS_HOME             4
#endif

static inline uint32_t portFailsafeMs(const DmxOutput* out) {
    if (out->failsafeto <= 0) {
        return SOURCE_TIMEOUT_MS;
    }
    return (uint32_t)out->failsafeto * 1000u;
}

/* Newest (max lastMs) among the top-priority sender indices in idxs[]. */
static int newestTop(const int* idxs, int n) {
    int best = idxs[0];
    uint32_t bestMs = g_senders[best].lastMs;
    for (int i = 1; i < n; i++) {
        uint32_t ms = g_senders[idxs[i]].lastMs;
        if (ms > bestMs) {
            bestMs = ms;
            best = idxs[i];
        }
    }
    return best;
}

/* Per-channel max across senders[idxs[]] into zero-init out[]. */
static void mergeMax(const int* idxs, int n, uint8_t* out, int slots) {
    for (int c = 0; c < slots; c++) {
        uint8_t m = 0;
        for (int t = 0; t < n; t++) {
            const SenderEntry* s = &g_senders[idxs[t]];
            if (c < (int)s->dataLen && s->data[c] > m) {
                m = s->data[c];
            }
        }
        out[c] = m;
    }
}

/* Apply the configured loss mode on source loss (spec 04 section 3).
 * Stats bookkeeping (outSrcLost / rxLossCount) and alert edges live in their
 * own modules (core.stats / sys.alert). */
static void applyLossMode(int outIdx, const DmxOutput* out) {
    switch (out->losemode) {
        case LOSS_ZERO:
            dmxBufWriteBegin(outIdx);
            memset(g_dmxBufState.buffers[outIdx].data + 1, 0, DMX_SLOT_COUNT);
            dmxBufWriteEnd(outIdx);
            break;
        case LOSS_PRESET:
            sceneRecall(out->lospreset, 0, outIdx);
            break;
        case LOSS_HOME:
            sceneRecallHome(outIdx);
            break;
        case LOSS_STOP:
        case LOSS_HOLD:
        default:
            break;
    }
}

int portAddress(const DmxOutput* out) {
    return (int)(((uint16_t)out->net << 8) | ((uint16_t)out->sub << 4) | (uint16_t)out->uni);
}

void mergeOutput(int outIdx, uint32_t nowMs) {
    if (outIdx < 0 || outIdx >= MAX_OUTPUTS) return;
    const DmxOutput* out = &cfg.outputs[outIdx];
    if (!out->en) return;

    uint16_t addr = (uint16_t)portAddress(out);
    uint32_t failsafeMs = portFailsafeMs(out);

    /* 1. Contributor selection: enabled, universe match, within failsafe. */
    int contrib[MAX_SENDERS];
    int nContrib = 0;
    uint8_t topPrio = 0;
    for (int i = 0; i < MAX_SENDERS; i++) {
        const SenderEntry* s = &g_senders[i];
        if (s->ip == 0) continue;
        if (s->universe != addr) continue;
        if ((uint32_t)(nowMs - s->lastMs) >= failsafeMs) continue;
        if (s->priority > topPrio) topPrio = s->priority;
        contrib[nContrib++] = i;
    }

    if (nContrib == 0) {
        applyLossMode(outIdx, out);
        return;
    }

    /* 2. Priority filter: only top-priority senders participate. */
    int top[MAX_SENDERS];
    int nTop = 0;
    for (int i = 0; i < nContrib; i++) {
        if (g_senders[contrib[i]].priority == topPrio) {
            top[nTop++] = contrib[i];
        }
    }
    if (nTop <= 0) return;

    uint8_t merged[DMX_SLOT_COUNT];
    memset(merged, 0, sizeof(merged));

    /* 3. Merge dispatch (spec 04 section 4 data flow, step 7). */
    int mode = out->mergemode;
    if (mode == MERGE_PRIORITY || (mode == MERGE_HTP && nTop >= 2)) {
        /* Per-channel max across top-priority senders; full 512 slots
         * (spec 04 section 13 flags 256 as a Priority quirk - avoided). */
        mergeMax(top, nTop, merged, DMX_SLOT_COUNT);
    } else {
        /* Off / LTP / LTP-Takeover, or single-source HTP: the newest
         * top-priority sender wins the whole frame. */
        int w = newestTop(top, nTop);
        const SenderEntry* s = &g_senders[w];
        size_t len = s->dataLen;
        if (len > DMX_SLOT_COUNT) len = DMX_SLOT_COUNT;
        memcpy(merged, s->data, len);
    }

    /* 4. Commit merged slots 1..512 into the seqlock live buffer (spec 03). */
    dmxBufWriteBegin(outIdx);
    memcpy(g_dmxBufState.buffers[outIdx].data + 1, merged, DMX_SLOT_COUNT);
    dmxBufWriteEnd(outIdx);
}