#include "sacn.h"
#include "config_schema.h"
#include "merge_engine.h"
#include "frame_router.h"
#include "dmx_buffer.h"
#include "sender_tracker.h"
#include "stats.h"
#include "artnet.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

const uint8_t ACN_PACKET_ID[12] = {
    0x41, 0x53, 0x43, 0x2d, 0x45, 0x31, 0x2e, 0x31,
    0x37, 0x00, 0x00, 0x00
};

WiFiUDP sacnUdp[MAX_OUTPUTS];
uint8_t sacnBuf[638];

static uint8_t sacnOwnCid[16] = {0};
static uint32_t sacnDiscLastMs = 0;
static uint32_t sacnSyncLossMs[MAX_OUTPUTS] = {0};

static uint16_t sacnUniverseFor(int outIdx) {
    int su = cfg.outputs[outIdx].sacnUniverse;
    if (su <= 0) su = cfg.outputs[outIdx].universe + 1;
    return (uint16_t)su;
}

static void initCid() {
    Preferences p; p.begin("dmxgw", false);
    if (!p.getBytes("cid", sacnOwnCid, 16)) {
        uint32_t chipId = (uint32_t)ESP.getEfuseMac();
        memset(sacnOwnCid, 0, 16);
        sacnOwnCid[0] = 0x52;
        sacnOwnCid[1] = 0x45;
        sacnOwnCid[2] = 0x44;
        sacnOwnCid[3] = 0x49;
        sacnOwnCid[4] = 0x3A;
        sacnOwnCid[5] = (chipId >> 24) & 0xFF;
        sacnOwnCid[6] = (chipId >> 16) & 0xFF;
        sacnOwnCid[7] = (chipId >> 8) & 0xFF;
        sacnOwnCid[8] = chipId & 0xFF;
        p.putBytes("cid", sacnOwnCid, 16);
    }
    p.end();
}

void startSacn() {
    initCid();
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        sacnUdp[i].stop();
        if (!cfg.outputs[i].enabled) continue;
        uint16_t su = sacnUniverseFor(i);
        uint8_t  univHigh = (su >> 8) & 0xFF;
        uint8_t  univLow  = su & 0xFF;
        IPAddress mcast(239, 255, univHigh, univLow);
        sacnUdp[i].beginMulticast(mcast, 5568);
        Serial.printf("[sACN] out%d universe %u  multicast 239.255.%u.%u:5568\n",
                      i, su, univHigh, univLow);
    }
}

void readSacnSocket(int outIdx) {
    WiFiUDP& udp = sacnUdp[outIdx];
    for (int guard = 0; guard < 16; guard++) {
        int pktLen = udp.parsePacket();
        if (pktLen <= 0) return;
        if (pktLen < SACN_MIN_LEN) { static uint8_t junk[638]; udp.read(junk, sizeof(junk)); continue; }

        uint32_t senderIp = (uint32_t)udp.remoteIP();
        int n = udp.read(sacnBuf, sizeof(sacnBuf));
        if (n < SACN_MIN_LEN) continue;
        if (memcmp(sacnBuf + SACN_ACN_ID_OFF, ACN_PACKET_ID, 12) != 0) continue;

        uint32_t rootVec = ((uint32_t)sacnBuf[SACN_ROOT_VEC_OFF    ] << 24)
                         | ((uint32_t)sacnBuf[SACN_ROOT_VEC_OFF + 1] << 16)
                         | ((uint32_t)sacnBuf[SACN_ROOT_VEC_OFF + 2] <<  8)
                         |  (uint32_t)sacnBuf[SACN_ROOT_VEC_OFF + 3];
        if (rootVec != 0x00000004u) continue;

        uint32_t frameVec = ((uint32_t)sacnBuf[SACN_FRAME_VEC_OFF    ] << 24)
                          | ((uint32_t)sacnBuf[SACN_FRAME_VEC_OFF + 1] << 16)
                          | ((uint32_t)sacnBuf[SACN_FRAME_VEC_OFF + 2] <<  8)
                          |  (uint32_t)sacnBuf[SACN_FRAME_VEC_OFF + 3];

        // Universe Discovery (receiver consumes/discards)
        if (frameVec == 0x00000004u) continue;

        // Standard streaming data
        if (frameVec != 0x00000002u) continue;

        uint16_t universe = ((uint16_t)sacnBuf[SACN_UNIVERSE_OFF] << 8)
                           |  sacnBuf[SACN_UNIVERSE_OFF + 1];

        if (sacnBuf[SACN_STARTCODE_OFF] != 0x00) continue;

        uint8_t priority = sacnBuf[SACN_PRIORITY_OFF];
        uint16_t payloadLen = (uint16_t)(n - SACN_DATA_OFF);
        if (payloadLen > 512) payloadLen = 512;

        // Convert sACN universe (1-based) back to the 15-bit Art-Net port address
        int artUniv = (int)universe - 1;
        routeFrame(artUniv, sacnBuf + SACN_DATA_OFF, payloadLen, senderIp, 1, priority);
    }
}

void readSacn() {
    uint32_t now = millis();

    // Periodic Universe Discovery response (receiver-only: no transmission)
    if (now - sacnDiscLastMs >= 10000) {
        sacnDiscLastMs = now;
    }

    // Process sync staging on outputs with sync address set
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (!cfg.outputs[i].enabled) continue;
        if (sacnSyncAddress[i] != 0 && sacnStagedValid[i]) {
            uint32_t syncMs = now - sacnSyncLossMs[i];
            if (syncMs < 500) continue;
            if (syncMs >= 2500) {
                routeFrame((int)portAddress(cfg.outputs[i]), sacnStaged[i], 512, 0, 1, DEFAULT_PRIORITY);
                sacnStagedValid[i] = false;
                sacnSyncAddress[i] = 0;
                sacnSyncLossMs[i] = 0;
            }
        }
    }

    for (int i = 0; i < MAX_OUTPUTS; i++)
        if (cfg.outputs[i].enabled) readSacnSocket(i);
}
