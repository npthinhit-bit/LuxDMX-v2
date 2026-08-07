#include "sender_tracker.h"
#include "stats.h"
#include <Arduino.h>
#include <string.h>

Sender senders[MAX_SENDERS] = {};

bool universeMapped(int universe) {
    for (int o = 0; o < MAX_OUTPUTS; o++)
        if (cfg.outputs[o].enabled && cfg.outputs[o].universe == universe) return true;
    return false;
}

void updateSender(uint32_t ip, uint8_t proto, int16_t universe,
                  uint8_t priority, const uint8_t* data, uint16_t length) {
    uint32_t now = millis();
    int slot = -1;
    for (int i = 0; i < MAX_SENDERS; i++)
        if (senders[i].ip == ip && senders[i].proto == proto) { slot = i; break; }
    bool fresh = false;
    if (slot < 0) {
        for (int i = 0; i < MAX_SENDERS; i++)
            if (senders[i].ip == 0) { slot = i; break; }
    }
    if (slot < 0) {
        for (int i = 0; i < MAX_SENDERS; i++)
            if (!universeMapped(senders[i].universe)) { slot = i; break; }
        if (slot < 0) {
            slot = 0;
            for (int i = 1; i < MAX_SENDERS; i++)
                if (senders[i].lastMs < senders[slot].lastMs) slot = i;
        }
    }
    Sender& s = senders[slot];
    if (s.ip != ip || s.proto != proto) fresh = true;
    if (fresh) {
        memset(s.data, 0, sizeof(s.data));
        s.ip = ip; s.proto = proto;
        s.winMs = now; s.winCnt = 0; s.fps = 0.0f;
    }
    s.lastMs   = now;
    s.universe = universe;
    s.priority = priority;
    s.dataLen  = length < 512 ? length : 512;
    memcpy(s.data, data, s.dataLen);
    for (int o = 0; o < MAX_OUTPUTS; o++)
        if (cfg.outputs[o].enabled && cfg.outputs[o].universe == universe) inFrameCnt[o]++;
    s.winCnt++;
    if (now - s.winMs >= 1000) {
        s.fps   = (float)s.winCnt * 1000.0f / (float)(now - s.winMs);
        s.winCnt = 0;
        s.winMs  = now;
    }
}

uint8_t activeSenderCount() {
    uint32_t now = millis();
    uint8_t n = 0;
    for (int i = 0; i < MAX_SENDERS; i++)
        if (senders[i].ip != 0 && now - senders[i].lastMs < 5000) n++;
    return n;
}

int sourcesOnUniverse(int universe, uint32_t windowMs) {
    uint32_t now = millis();
    int n = 0;
    for (int i = 0; i < MAX_SENDERS; i++) {
        const Sender& s = senders[i];
        if (s.ip == 0 || s.universe != universe) continue;
        if (now - s.lastMs < windowMs) n++;
    }
    return n;
}

bool hasConflict() {
    for (int o = 0; o < MAX_OUTPUTS; o++) {
        if (!cfg.outputs[o].enabled || cfg.outputs[o].mergeMode != MERGE_OFF) continue;
        if (sourcesOnUniverse(cfg.outputs[o].universe, SOURCE_TIMEOUT_MS) > 1) return true;
    }
    return false;
}

bool isMerging() {
    for (int o = 0; o < MAX_OUTPUTS; o++) {
        if (!cfg.outputs[o].enabled || cfg.outputs[o].mergeMode == MERGE_OFF) continue;
        if (sourcesOnUniverse(cfg.outputs[o].universe, SOURCE_TIMEOUT_MS) > 1) return true;
    }
    return false;
}

uint8_t sourceStatus() {
    if (hasConflict()) return 1;
    if (isMerging()) return 2;
    return 0;
}
