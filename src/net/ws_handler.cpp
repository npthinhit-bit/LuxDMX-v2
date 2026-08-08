#include "ws_handler.h"
#include "ws_frame.h"
#include "stats.h"
#include "output_init.h"
#include "dmx_buffer.h"
#include "rdm_engine.h"
#include "rdm_disc.h"
#include "config_schema.h"
#include "sender_tracker.h"
#include "scene_engine.h"
#include "sys_platform.h"
#include <Arduino.h>

extern uint16_t identifyCh;
extern uint32_t identifyUntil;

static rdm_uid_t g_pendingUid = {0};
static int g_pendingAction = 0;  // 0=none, 1=discover, 2=setaddr, 3=identify, 4=setpers, 5=setlabel
static uint16_t g_pendingAddr = 0;
static uint8_t g_pendingPers = 0;
static char g_pendingLabel[33] = {0};

static bool parseUid(const String& msg, rdm_uid_t& uid) {
    int idx = msg.indexOf("\"uid\":\"");
    if (idx < 0) return false;
    idx += 6;
    int end = msg.indexOf('"', idx);
    if (end < 0) return false;
    String hex = msg.substring(idx, end);
    if (hex.length() >= 12) {
        uint32_t man = 0, dev = 0;
        sscanf(hex.substring(0, 4).c_str(), "%04x", &man);
        sscanf(hex.substring(4, 12).c_str(), "%08x", &dev);
        uid.man_id = (uint16_t)man;
        uid.dev_id = dev;
        return true;
    }
    return false;
}

void handleWsTextRdm(const String& msg) {
    if (msg.indexOf("\"rdm\"") < 0) return;

    if (msg.indexOf("\"discover\"") >= 0) {
        g_pendingAction = 1;
        return;
    }
    if (msg.indexOf("\"setaddr\"") >= 0) {
        rdm_uid_t uid;
        if (!parseUid(msg, uid)) return;
        g_pendingUid = uid;
        int idx = msg.indexOf("\"addr\":");
        if (idx < 0) return;
        g_pendingAddr = (uint16_t)msg.substring(idx + 6).toInt();
        g_pendingAction = 2;
        return;
    }
    if (msg.indexOf("\"identify\"") >= 0) {
        rdm_uid_t uid;
        if (!parseUid(msg, uid)) return;
        g_pendingUid = uid;
        g_pendingAction = 3;
        return;
    }
    if (msg.indexOf("\"setpers\"") >= 0) {
        rdm_uid_t uid;
        if (!parseUid(msg, uid)) return;
        int idx = msg.indexOf("\"pers\":");
        if (idx < 0) return;
        g_pendingPers = (uint8_t)msg.substring(idx + 6).toInt();
        g_pendingUid = uid;
        g_pendingAction = 4;
        return;
    }
    if (msg.indexOf("\"setlabel\"") >= 0) {
        rdm_uid_t uid;
        if (!parseUid(msg, uid)) return;
        int idx = msg.indexOf("\"label\":\"");
        if (idx < 0) return;
        idx += 9;
        int end = msg.indexOf('"', idx);
        if (end < 0) return;
        int n = end - idx; if (n > 32) n = 32;
        msg.substring(idx, end).toCharArray(g_pendingLabel, n + 1);
        g_pendingUid = uid;
        g_pendingAction = 5;
        return;
    }
}

// Called from loop() to execute queued RDM operations (core 0, after DMX task
// has released the RMT channel).
void rdmWsProcessQueued() {
    if (g_pendingAction == 0) return;

    int action = g_pendingAction;
    g_pendingAction = 0;

    switch (action) {
        case 1: {
            // Force discovery on the selected RDM output
            rdmRmtSelect(rdmOut);
            rdm_uid_t found[MAX_SENDERS];
            int count = rdmRmtDiscover(found, MAX_SENDERS);
            rdmCount = count;
            for (int i = 0; i < count && i < RDM_TOD_MAX; i++) {
                rdmTod[i] = found[i];
            }
            for (int i = 0; i < count; i++) {
                Serial.printf("[RDM] discovered " UIDSTR "\n", UID2STR(found[i]));
            }
            rdmPollDirty = true;
            break;
        }
        case 2: {
            rdm_ack_t ack;
            rdmOpSetAddr(g_pendingUid, g_pendingAddr, &ack);
            rdmPollDirty = true;
            break;
        }
        case 3: {
            rdm_ack_t ack;
            rdmOpSetIdentify(g_pendingUid, true, &ack);
            break;
        }
        case 4: {
            rdm_ack_t ack;
            rdmOpSetPersonality(g_pendingUid, g_pendingPers, &ack);
            break;
        }
        case 5: {
            rdm_ack_t ack;
            rdmOpSetString(g_pendingUid, RDM_PID_DEVICE_LABEL, g_pendingLabel, &ack);
            memset(g_pendingLabel, 0, sizeof(g_pendingLabel));
            break;
        }
    }
}

void handleWsText(const char* payload, size_t len) {
    String msg(payload, len);
    if (msg.indexOf("\"viewout\"") >= 0) {
        int k = msg.indexOf("\"out\":");
        if (k >= 0) {
            int o = msg.substring(k + 6).toInt();
            if (o >= 0 && o < MAX_OUTPUTS && cfg.outputs[o].enabled) monitorOut = o;
        }
        return;
    }
    if (msg.indexOf("\"blackout\"") >= 0) {
        const int mo = viewOutput();
        dmxBufWriteBegin(mo); memset(&dmxBuffers[mo].data[1], 0, 512); dmxBufWriteEnd(mo);
        return;
    }
    if (msg.indexOf("\"mode\"") >= 0) {
        manualMode = (msg.indexOf("true") >= 0);
        return;
    }
    if (msg.indexOf("\"identify\"") >= 0) {
        int chIdx = msg.indexOf("\"ch\":");
        if (chIdx < 0) return;
        int ch = msg.substring(chIdx + 5).toInt();
        if (ch < 1 || ch > 512) return;
        identifyCh = (uint16_t)ch;
        identifyUntil = millis() + IDENTIFY_MS;
        return;
    }
    if (msg.indexOf("\"set\"") >= 0) {
        int chIdx  = msg.indexOf("\"ch\":");
        int valIdx = msg.indexOf("\"val\":");
        if (chIdx < 0 || valIdx < 0) return;
        int ch  = msg.substring(chIdx  + 5).toInt();
        int val = msg.substring(valIdx + 6).toInt();
        if (ch < 1 || ch > 512) return;
        const int mo = viewOutput();
        dmxBufWriteBegin(mo);
        dmxBufWriteEndSet(mo, ch, (uint8_t)constrain(val, 0, 255));
        return;
    }
    if (msg.indexOf("\"scene\"") >= 0) {
        int idx = msg.indexOf("\"play\":");
        if (idx < 0) return;
        int sceneIdx = msg.substring(idx + 6).toInt();
        int fadeIdx = msg.indexOf("\"fade\":");
        uint16_t fadeMs = 0;
        if (fadeIdx >= 0) fadeMs = (uint16_t)msg.substring(fadeIdx + 6).toInt();
        sceneTriggerPlay(sceneIdx, fadeMs);
        Serial.printf("[SCENE] triggered scene %d fade=%dms\n", sceneIdx, fadeMs);
        return;
    }
    if (msg.indexOf("\"saveScene\"") >= 0) {
        int idx = msg.indexOf("\"idx\":");
        if (idx < 0) return;
        int sceneIdx = msg.substring(idx + 5).toInt();
        if (sceneIdx < 0 || sceneIdx >= MAX_SCENES) return;
        const int mo = viewOutput();
        dmxBufSnapshot(mo, g_scenes[sceneIdx].data[mo]);
        const char* nameKey = "\"name\":\"";
        int nk = msg.indexOf(nameKey);
        if (nk >= 0) {
            nk += 8;
            int ne = msg.indexOf('"', nk);
            if (ne > nk) {
                int n = ne - nk; if (n > 31) n = 31;
                msg.substring(nk, ne).toCharArray(g_scenes[sceneIdx].name, n + 1);
            }
        }
        sceneSaveNvs(sceneIdx);
        Serial.printf("[SCENE] saved scene %d\n", sceneIdx);
        return;
    }
    if (msg.indexOf("\"clearScene\"") >= 0) {
        int idx = msg.indexOf("\"idx\":");
        if (idx < 0) return;
        int sceneIdx = msg.substring(idx + 5).toInt();
        sceneEraseNvs(sceneIdx);
        return;
    }
    handleWsTextRdm(msg);
}
