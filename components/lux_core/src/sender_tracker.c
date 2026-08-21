/*
 * spec 05 - Sender Tracker
 *
 * Maintains a 16-slot table of active network DMX sources. updateSender() is
 * called by the Frame Router (core 0) on every routed frame; the merge engine
 * (core 1) reads the cached entries without a lock (spec 05 section 10).
 *
 * No logging is performed by this module (spec 05 section 7).
 */
#include "sender_tracker.h"
#include "config_engine.h"   /* cfg, MAX_OUTPUTS, DmxOutput            */
#include "esp_timer.h"       /* esp_timer_get_time -- monotonic ms     */
#include <string.h>

/* Static sender table (spec 05 section 9: static DRAM, no heap). */
SenderEntry g_senders[MAX_SENDERS];

/* Monotonic system uptime in ms. esp_timer_get_time() returns microseconds
 * since boot; divide by 1000. This is the facility referenced as millis()
 * across the spec series (spec 13, 04). */
static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Build the 15-bit ArtNet port address for output idx:
 * net(7) | sub(4) | uni(4) (spec 05 section 5, table row). */
static uint16_t outputPortAddress(int idx) {
    const DmxOutput* o = &cfg.outputs[idx];
    return (uint16_t)(((uint16_t)o->net << 8) | ((uint16_t)o->sub << 4) | (uint16_t)o->uni);
}

/* Does any enabled output carry this 15-bit universe? (spec 05 section 4
 * tier 3 eviction test and section 5 config integration). */
static bool universeMapped(uint16_t universe) {
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (!cfg.outputs[i].en) continue;
        if (outputPortAddress(i) == universe) return true;
    }
    return false;
}

/* Shared baseline guard (spec 05 section 7): no enabled outputs, or every
 * enabled output is in MERGE_OFF -- neither conflict nor merging is reported. */
static bool outputsActive(void) {
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (cfg.outputs[i].en) return true;
    }
    return false;
}

static bool allOutputsMergeOff(void) {
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (cfg.outputs[i].en && cfg.outputs[i].mergemode != 0) return false;
    }
    return true;
}

/* Zero the table; called once at startup (spec 05 section 6). Every slot then
 * has ip==0 (Empty). */
void senderTrackerInit(void) {
    memset(g_senders, 0, sizeof(g_senders));
}

void updateSender(uint32_t ip, uint8_t proto, uint16_t universe,
                  uint8_t priority, const uint8_t* data, uint16_t length) {
    uint32_t now = now_ms();

    /* spec 05 section 4 step 6: clamp length to the cache buffer. */
    if (length > DMX_SLOT_COUNT) length = DMX_SLOT_COUNT;

    /* Tier 1: replace in place if a sender with the same (ip, proto) exists. */
    int slot = -1;
    for (int i = 0; i < MAX_SENDERS; i++) {
        if (g_senders[i].ip == ip && g_senders[i].proto == proto) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        /* Tier 2: first empty slot. */
        for (int i = 0; i < MAX_SENDERS; i++) {
            if (g_senders[i].ip == 0) {
                slot = i;
                break;
            }
        }
    }

    if (slot < 0) {
        /* Tier 3: a slot whose universe is no longer mapped to any enabled
         * output (e.g. output universe was remapped live). */
        for (int i = 0; i < MAX_SENDERS; i++) {
            SenderEntry* s = &g_senders[i];
            if (s->ip != 0 && !universeMapped((uint16_t)s->universe)) {
                slot = i;
                break;
            }
        }
    }

    if (slot < 0) {
        /* Tier 4: oldest sender by lastMs (spec 05 section 7: always succeeds). */
        slot = 0;
        uint32_t oldest = g_senders[0].lastMs;
        for (int i = 1; i < MAX_SENDERS; i++) {
            if (g_senders[i].lastMs < oldest) {
                oldest = g_senders[i].lastMs;
                slot = i;
            }
        }
    }
    if (slot < 0) slot = 0;  /* safety (MAX_SENDERS is always >= 1) */

    SenderEntry* s = &g_senders[slot];
    bool fresh = !(g_senders[slot].ip == ip && g_senders[slot].proto == proto);

    /* spec 05 section 4 step 5: a newly allocated/evicted slot is reset before
     * the new frame is written -- zero data and restart the FPS window. */
    if (fresh) {
        memset(s->data, 0, DMX_SLOT_COUNT);
        s->winMs = now;
        s->winCnt = 0;
        s->fps  = 0;
    }

    /* spec 05 section 4 step 6: cache metadata, then data. dataLen is written
     * AFTER the memcpy so a core-1 reader never pairs a stale length with new
     * data (spec 05 section 10 torn-read invariant). */
    s->ip = ip;
    s->proto = proto;
    s->universe = universe;
    s->priority = priority;
    if (data != NULL && length > 0) {
        memcpy(s->data, data, length);
    }
    s->dataLen = length;
    s->lastMs = now;

    /* spec 05 section 4 step 7: per-output inFrameCnt would be bumped here for
     * every enabled output whose universe matches -- that counter lives in the
     * output-stats layer, not the sender table. */

    /* spec 05 section 4 step 8: rolling 1 s FPS window. */
    s->winCnt++;
    if ((uint32_t)(now - s->winMs) >= FPS_WINDOW_MS) {
        s->fps = (uint16_t)s->winCnt;
        s->winMs = now;
        s->winCnt = 1;  /* this frame opens the new window */
    }
}

int activeSenderCount(void) {
    uint32_t now = now_ms();
    int count = 0;
    for (int i = 0; i < MAX_SENDERS; i++) {
        SenderEntry* s = &g_senders[i];
        if (s->ip != 0 && (uint32_t)(now - s->lastMs) <= ACTIVE_WINDOW_MS) {
            count++;
        }
    }
    return count;
}

int sourcesOnUniverse(uint16_t universe, uint32_t windowMs) {
    uint32_t now = now_ms();
    int count = 0;
    for (int i = 0; i < MAX_SENDERS; i++) {
        SenderEntry* s = &g_senders[i];
        if (s->ip != 0 &&
            s->universe == universe &&
            (uint32_t)(now - s->lastMs) <= windowMs) {
            count++;
        }
    }
    return count;
}

/* Merging = a merge-enabled output has >1 active source (spec 05 section 2). */
static bool isMerging(void) {
    if (!outputsActive() || allOutputsMergeOff()) return false;
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (!cfg.outputs[i].en || cfg.outputs[i].mergemode == 0) continue;
        if (sourcesOnUniverse(outputPortAddress(i), SOURCE_TIMEOUT_MS) > 1) return true;
    }
    return false;
}

/* Conflict = any enabled output has >1 active source (merge off for that output). */
static bool hasConflict(void) {
    if (!outputsActive() || allOutputsMergeOff()) return false;
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (!cfg.outputs[i].en) continue;
        if (sourcesOnUniverse(outputPortAddress(i), SOURCE_TIMEOUT_MS) > 1) return true;
    }
    return false;
}

int sourceStatus(void) {
    if (isMerging())   return SRC_STATUS_MERGING;
    if (hasConflict()) return SRC_STATUS_CONFLICT;
    return SRC_STATUS_NORMAL;
}