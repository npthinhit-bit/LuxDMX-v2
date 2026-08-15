#include "sacn.h"
#include "sacn_pkt_queue.h"
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
static WiFiUDP sacnSyncUdp;

static uint16_t sacnUniverseFor(int outIdx) {
    int su = cfg.outputs[outIdx].sacnUniverse;
    if (su <= 0) su = cfg.outputs[outIdx].universe + 1;
    return (uint16_t)su;
}

static uint16_t sacnSyncUniverseFor(int outIdx) {
    return (uint16_t)cfg.outputs[outIdx].sacnSync;
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

// --- sACN Universe Discovery (E1.31) transmit ---------------------------------
// Discovery packet (E1.31 6.2.6 / 6.3.3): root vec 0x00000004, framing vec 0x00000007,
// data = page count, page number, then universe IDs (1 byte each; extended 2-byte form exists).
#define SACN_DISC_ROOT_VEC   0x00000004u
#define SACN_DISC_FRAME_VEC  0x00000007u
#define SACN_DISC_MAX_UNIV   509u
#define SACN_DISC_INTERVAL_MS 10000u

static WiFiUDP sacnDiscUdp;

static void sendSacnDiscovery() {
    uint16_t univs[MAX_OUTPUTS];
    int nuniv = 0;
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (cfg.outputs[i].enabled) univs[nuniv++] = sacnUniverseFor(i);
    }
    if (nuniv == 0) return;

    uint8_t pkt[638];
    memset(pkt, 0, sizeof(pkt));

    memcpy(pkt + SACN_ACN_ID_OFF, ACN_PACKET_ID, 12);
    pkt[SACN_ROOT_VEC_OFF    ] = (SACN_DISC_ROOT_VEC >> 24) & 0xFF;
    pkt[SACN_ROOT_VEC_OFF + 1] = (SACN_DISC_ROOT_VEC >> 16) & 0xFF;
    pkt[SACN_ROOT_VEC_OFF + 2] = (SACN_DISC_ROOT_VEC >> 8) & 0xFF;
    pkt[SACN_ROOT_VEC_OFF + 3] = SACN_DISC_ROOT_VEC & 0xFF;
    uint16_t rootLen = (uint16_t)(sizeof(pkt) - 20);
    pkt[SACN_ROOT_VEC_OFF - 2] = (rootLen >> 8) & 0xFF;
    pkt[SACN_ROOT_VEC_OFF - 1] = rootLen & 0xFF;

    uint32_t frameVec = SACN_DISC_FRAME_VEC;
    pkt[SACN_FRAME_VEC_OFF    ] = (frameVec >> 24) & 0xFF;
    pkt[SACN_FRAME_VEC_OFF + 1] = (frameVec >> 16) & 0xFF;
    pkt[SACN_FRAME_VEC_OFF + 2] = (frameVec >> 8) & 0xFF;
    pkt[SACN_FRAME_VEC_OFF + 3] = frameVec & 0xFF;
    memcpy(pkt + SACN_FRAME_VEC_OFF + 4, sacnOwnCid, 16);

    int dataOff = SACN_FRAME_VEC_OFF + 4 + 16 + 4; // frame_vec + cid + reserved + universe
    int pages = (nuniv + SACN_DISC_MAX_UNIV - 1) / SACN_DISC_MAX_UNIV;
    if (pages > 255) pages = 255;
    for (int page = 0; page < pages; page++) {
        pkt[dataOff]     = (uint8_t)pages;
        pkt[dataOff + 1] = (uint8_t)page;
        int start = page * SACN_DISC_MAX_UNIV;
        int count = nuniv - start;
        if (count > (int)SACN_DISC_MAX_UNIV) count = (int)SACN_DISC_MAX_UNIV;
        for (int j = 0; j < count; j++)
            pkt[dataOff + 2 + j] = (uint8_t)(univs[start + j] & 0xFF);
        IPAddress mcast(239, 255, 255, 222);
        sacnDiscUdp.beginPacket(mcast, 5568);
        sacnDiscUdp.write(pkt, SACN_DATA_OFF);
        sacnDiscUdp.endPacket();
        sacnDiscUdp.stop();
        delay(1);
    }
    Serial.printf("[sACN] sent Universe Discovery (%d pages, %d universes)\n", pages, nuniv);
}

void startSacn() {
    sacnPktQueueInit();
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

        uint16_t ssu = sacnSyncUniverseFor(i);
        if (ssu > 0) {
            uint8_t sh = (ssu >> 8) & 0xFF, sl = ssu & 0xFF;
            IPAddress smcast(239, 255, sh, sl);
            if (!sacnSyncUdp.beginMulticast(smcast, 5568)) {
                Serial.printf("[sACN] ERROR: failed to join sync universe %u multicast\n", ssu);
            }
            sacnSyncAddress[i] = ssu;
            Serial.printf("[sACN] out%d sync universe %u  multicast 239.255.%u.%u:5568\n",
                          i, ssu, sh, sl);
        } else {
            sacnSyncAddress[i] = 0;
        }
    }
}

static void handleSacnPacket(int outIdx, uint32_t senderIp, const uint8_t* p, int n) {
    (void)outIdx;
    if (n < SACN_MIN_LEN) return;
    if (memcmp(p + SACN_ACN_ID_OFF, ACN_PACKET_ID, 12) != 0) return;

    uint32_t rootVec = ((uint32_t)p[SACN_ROOT_VEC_OFF    ] << 24)
                     | ((uint32_t)p[SACN_ROOT_VEC_OFF + 1] << 16)
                     | ((uint32_t)p[SACN_ROOT_VEC_OFF + 2] <<  8)
                     |  (uint32_t)p[SACN_ROOT_VEC_OFF + 3];
    if (rootVec != 0x00000004u) return;

    uint32_t frameVec = ((uint32_t)p[SACN_FRAME_VEC_OFF    ] << 24)
                      | ((uint32_t)p[SACN_FRAME_VEC_OFF + 1] << 16)
                      | ((uint32_t)p[SACN_FRAME_VEC_OFF + 2] <<  8)
                      |  (uint32_t)p[SACN_FRAME_VEC_OFF + 3];

    // Universe Discovery (receiver consumes/discards)
    if (frameVec == SACN_FRAME_VEC_DISCOVERY) return;

    // Standard streaming data
    if (frameVec != SACN_FRAME_VEC_STREAM) return;

    uint16_t universe = ((uint16_t)p[SACN_UNIVERSE_OFF] << 8)
                       |  p[SACN_UNIVERSE_OFF + 1];

    if (p[SACN_STARTCODE_OFF] != 0x00) return;

    uint8_t priority = p[SACN_PRIORITY_OFF];
    uint16_t payloadLen = (uint16_t)(n - SACN_DATA_OFF);
    if (payloadLen > 512) payloadLen = 512;

    // Convert sACN universe (1-based) back to the 15-bit Art-Net port address
    int artUniv = (int)universe - 1;

    // If this output uses Stream Sync, stage the frame instead of routing it
    // directly. The staged frame is committed by the sync-loss timeout path
    // below (or immediately flushed on next sync packet).
    bool staged = false;
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (!cfg.outputs[i].enabled) continue;
        if (sacnSyncAddress[i] != 0 && portAddress(cfg.outputs[i]) == (uint16_t)artUniv) {
            uint16_t copyLen = payloadLen > 512 ? 512 : payloadLen;
            memcpy(sacnStaged[i], p + SACN_DATA_OFF, copyLen);
            sacnStagedLen[i] = copyLen;
            sacnStagedValid[i] = true;
            sacnSyncLossMs[i] = (uint32_t)millis();
            updateSender(senderIp, 1, (int16_t)artUniv, priority, p + SACN_DATA_OFF, payloadLen);
            staged = true;
        }
    }
    if (!staged)
        routeFrame(artUniv, p + SACN_DATA_OFF, payloadLen, senderIp, 1, priority);
}

void readSacnSocket(int outIdx) {
    WiFiUDP& udp = sacnUdp[outIdx];
    // Producer: recv at most 4 packets/socket per tick (was 16) and enqueue raw
    // packets; readSacn() drains + dispatches them. Back-pressure drops on full queue.
    for (int guard = 0; guard < 4; guard++) {
        int pktLen = udp.parsePacket();
        if (pktLen <= 0) return;
        uint32_t senderIp = (uint32_t)udp.remoteIP();
        uint8_t frame[SACN_PKT_MAX];
        int cap = (pktLen < (int)SACN_PKT_MAX) ? pktLen : (int)SACN_PKT_MAX;
        int n = udp.read(frame, cap);
        if (n <= 0) continue;
        if (n < SACN_MIN_LEN) continue;          // too short to be a valid sACN frame
        sacnPktPush((uint16_t)outIdx, senderIp, frame, (uint16_t)n);
    }
}

void readSacn() {
    uint32_t now = millis();

    // Periodic Universe Discovery announcement (E1.31 section 6.3)
    if (now - sacnDiscLastMs >= SACN_DISC_INTERVAL_MS) {
        sacnDiscLastMs = now;
        sendSacnDiscovery();
    }

    // Process sync staging on outputs with sync address set
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (!cfg.outputs[i].enabled) continue;
        if (sacnSyncAddress[i] != 0 && sacnStagedValid[i]) {
            uint32_t syncMs = now - sacnSyncLossMs[i];
            if (syncMs < 500) continue;
            if (syncMs >= 2500) {
                routeFrame((int)portAddress(cfg.outputs[i]), sacnStaged[i], sacnStagedLen[i], 0, 1, DEFAULT_PRIORITY);
                sacnStagedValid[i] = false;
                sacnSyncAddress[i] = 0;
                sacnSyncLossMs[i] = 0;
            }
        }
    }

    // Check for sACN sync packets (frame vector 0x00000003) on the sync socket.
    // A sync packet on the sync universe commits all staged frames for outputs
    // using that sync universe, with a 500ms commit grace (per roadmap). The
    // sync-loss timeout above handles the case where sync stops arriving.
    if (sacnSyncUdp.available()) {
        int n = sacnSyncUdp.parsePacket();
        if (n >= SACN_MIN_LEN) {
            int r = sacnSyncUdp.read(sacnBuf, sizeof(sacnBuf));
            if (r >= SACN_MIN_LEN &&
                memcmp(sacnBuf + SACN_ACN_ID_OFF, ACN_PACKET_ID, 12) == 0) {
                uint32_t fvec = ((uint32_t)sacnBuf[SACN_FRAME_VEC_OFF] << 24)
                              | ((uint32_t)sacnBuf[SACN_FRAME_VEC_OFF + 1] << 16)
                              | ((uint32_t)sacnBuf[SACN_FRAME_VEC_OFF + 2] << 8)
                              |  (uint32_t)sacnBuf[SACN_FRAME_VEC_OFF + 3];
                if (fvec == SACN_FRAME_VEC_SYNC) {
                    uint16_t su = ((uint16_t)sacnBuf[SACN_UNIVERSE_OFF] << 8)
                                | sacnBuf[SACN_UNIVERSE_OFF + 1];
                    for (int i = 0; i < MAX_OUTPUTS; i++) {
                        if (sacnSyncAddress[i] == su && sacnStagedValid[i]) {
                            routeFrame((int)portAddress(cfg.outputs[i]), sacnStaged[i], sacnStagedLen[i], 0, 1, DEFAULT_PRIORITY);
                            sacnStagedValid[i] = false;
                            sacnSyncLossMs[i] = now;
                        }
                    }
                }
            }
        }
    }

    // Producer: recv <= 4 packets/socket into the ring (per output).
    for (int i = 0; i < MAX_OUTPUTS; i++)
        if (cfg.outputs[i].enabled) readSacnSocket(i);
    // Consumer: drain the sACN ring and dispatch (parse + stage/route) each packet.
    SacnPkt sp;
    while (sacnPktPop(sp))
        handleSacnPacket(sp.outIdx, sp.srcIp, sp.data, sp.len);
}
