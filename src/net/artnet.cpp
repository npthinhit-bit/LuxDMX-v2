#include "artnet.h"
#include "art_pkt_queue.h"
#include "config_schema.h"
#include "frame_router.h"
#include "merge_engine.h"
#include "net_state.h"
#include "ethernet.h"
#include "rdm_engine.h"
#include "output_init.h"
#include "sender_tracker.h"
#include "dmx_buffer.h"
#include "scene_engine.h"
#include <Arduino.h>
#include <esp_wifi.h>
#include <fcntl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>

int            g_artSock       = -1;
bool           g_artRdmReady   = false;
uint32_t       g_nodeIp        = 0;
uint8_t        g_nodeMac[6]    = {0};
bool           g_artRdmEnabled = true;
uint16_t       g_artPolls      = 0;
uint8_t         g_bqPolicy     = 4;
volatile bool   g_bqDirty     = false;
volatile bool   g_artCfgDirty = false;

uint8_t  artSyncMode = 0;
uint32_t artSyncLastMs = 0;

// --- Art-Net TimeCode (E1.31 Annex C) ---
ArtTimeCode g_timecode = {0, 0, 0, 0, 0};
bool        g_timecodeValid = false;
bool        g_timecodeSend = false;
uint8_t     g_timecodeType = 0;
uint8_t     g_timecodeFps = 25;
static uint32_t g_tcLastSendMs = 0;

static void artHandlePacket(const uint8_t* p, int n, uint32_t ip);
static void sendTimecode();

void artRdmInit() {
    artPktQueueInit();
    g_nodeIp = (uint32_t)netLocalIP();
    uint8_t m[6];
    esp_wifi_get_mac(WIFI_IF_STA, m);
    memcpy(g_nodeMac, m, 6);
    g_artRdmEnabled = cfg.artnetRdm;
    g_timecodeSend = cfg.timecodeSend;
    g_timecodeType = cfg.timecodeType;
    g_timecodeFps  = cfg.timecodeFps;
    g_artSock = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_artSock >= 0) {
        int one = 1;
        lwip_setsockopt(g_artSock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        lwip_setsockopt(g_artSock, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
        // QoS/DSCP marking for time-sensitive DMX traffic
        if (cfg.dscpEnabled && cfg.dscpDmx >= 0 && cfg.dscpDmx <= 63) {
            int tos = (cfg.dscpDmx & 0x3F) << 2;  // DSCP in upper 6 bits of TOS
            lwip_setsockopt(g_artSock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
        }
        struct sockaddr_in a = {};
        a.sin_family = AF_INET; a.sin_port = htons(ARTNET_PORT);
        a.sin_addr.s_addr = INADDR_ANY;
        lwip_bind(g_artSock, (struct sockaddr*)&a, sizeof(a));
        int fl = lwip_fcntl(g_artSock, F_GETFL, 0);
        lwip_fcntl(g_artSock, F_SETFL, fl | O_NONBLOCK);
        g_artRdmReady = true;
    } else {
        Serial.println("[ART] socket() failed");
    }
    Serial.printf("[ART] Art-Net up on :%d (RDM %s)\n",
                  ARTNET_PORT, g_artRdmEnabled ? "on" : "off");
}

void artRdmPollRx() {
    if (!g_artRdmReady || g_artSock < 0) return;
    static uint8_t buf[640];
    // Producer: recv at most 8 packets/tick (was 64) and enqueue parsed Art-Net.
    for (int k = 0; k < 8; k++) {
        struct sockaddr_in src; socklen_t sl = sizeof(src);
        int n = lwip_recvfrom(g_artSock, buf, sizeof(buf), 0, (struct sockaddr*)&src, &sl);
        if (n <= 0) return;
        if (n >= 12 && memcmp(buf, ARTNET_ID, 8) == 0) {
            ArtPkt p;
            memset(&p, 0, sizeof(p));
            if (n > (int)ART_PKT_MAX) n = (int)ART_PKT_MAX;
            memcpy(p.data, buf, n);
            p.len   = (uint16_t)n;
            p.srcIp = (uint32_t)src.sin_addr.s_addr;
            artPktPush(p);          // drop on full (back-pressure under burst)
        }
    }
    // Send TimeCode if enabled
    sendTimecode();
}

// Consumer: dispatch all enqueued Art-Net packets. Called from netRxTask (core 0)
// right after artRdmPollRx(). Same task as the producer, so no cross-core locks.
void artPktDispatchAll() {
    ArtPkt p;
    while (artPktPop(p)) {
        artHandlePacket(p.data, p.len, p.srcIp);
    }
}

void artRdmDrainResponses() {
    // Response queue drained — placeholder for queued ArtRdm/ArtTod replies.
    // In a full implementation, the DMX task would push responses to a queue
    // and this function would send them over the Art-Net socket.
}

// --- Art-Net TimeCode send ---
static void sendTimecode() {
    if (!g_timecodeSend || g_artSock < 0) return;
    uint32_t now = millis();
    if (now - g_tcLastSendMs < 1000 / g_timecodeFps) return;
    g_tcLastSendMs = now;

    // Build ArtTimeCode packet (opcode 0x9700, 13 bytes total)
    uint8_t pkt[64];
    memset(pkt, 0, sizeof(pkt));
    memcpy(pkt, ARTNET_ID, 8);
    pkt[8] = ARTNET_OP_TIMECODE & 0xFF;   // opcode (little-endian)
    pkt[9] = (ARTNET_OP_TIMECODE >> 8) & 0xFF;
    pkt[10] = 0;  // flags
    // Data bytes 11-14: type(3b | hour 3b | minute 8b | second 8b | frame 8b)
    pkt[11] = ((g_timecode.type & 0x07) << 5) | ((g_timecode.hour & 0x07) << 2);
    pkt[12] = g_timecode.minute;
    pkt[13] = g_timecode.second;
    pkt[14] = g_timecode.frame;

    struct sockaddr_in dst = {};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(ARTNET_PORT);
    dst.sin_addr.s_addr = 0xFFFFFFFFu;  // broadcast
    lwip_sendto(g_artSock, pkt, 15, 0, (struct sockaddr*)&dst, sizeof(dst));
}

static void artHandlePacket(const uint8_t* p, int n, uint32_t ip) {
    uint16_t op = p[8] | (p[9] << 8);

    if (op == ARTNET_OP_SYNC) {
        artSyncMode = false;   // SYNC commits the staged frames and returns to immediate mode
        commitArtSyncStaged();
        artnetBridgeDispatch(op, p, n, ip);
        return;
    }

    if (artSyncMode) {
        uint32_t now = millis();
        if (now - artSyncLastMs > ARTSYNC_TIMEOUT_MS) {
            Serial.println("[ART] ArtSync timeout, falling back to immediate");
            artSyncMode = false;
        }
    }

    if (op == ARTNET_OP_POLL || op == ARTNET_OP_ADDRESS || op == ARTNET_OP_IPPROG ||
        op == ARTNET_OP_TODREQUEST || op == ARTNET_OP_RDM) {
        artnetBridgeDispatch(op, p, n, ip);
        return;
    }

    if (op == ARTNET_OP_DMX) {
        if (n < 18 || cfg.protocol == 1) return;
        uint16_t universe = p[14] | (p[15] << 8);
        uint16_t length   = (p[16] << 8) | p[17];
        if (length > 512) length = 512;
        if (18 + length > n) length = n - 18;
        uint8_t priority = (n >= 60) ? p[59] : DEFAULT_PRIORITY;

        if (artSyncMode) {
            for (int i = 0; i < MAX_OUTPUTS; i++) {
                if (!cfg.outputs[i].enabled || portAddress(cfg.outputs[i]) != universe) continue;
                memcpy(dmxStaged[i], p + 18, length);
                dmxStagedLen[i] = length;
                dmxStagedValid[i] = true;
            }
            updateSender(ip, 0, (int16_t)universe, priority, p + 18, length);
            return;
        }
        routeFrame((int)universe, p + 18, length, ip, 0, priority);
        return;
    }

    // --- Art-Net TimeCode (opcode 0x9700) ---
    if (op == ARTNET_OP_TIMECODE && n >= 13) {
        // Layout: [8]id [9]op [10]flags [11]type [12]hour [13]min [14]sec [15]frame
        g_timecode.type    = (p[11] >> 5) & 0x07;
        g_timecode.hour    = (p[11] >> 2) & 0x07;
        g_timecode.minute  = p[12];
        g_timecode.second  = p[13];
        g_timecode.frame   = p[14];
        g_timecodeValid    = true;
        // Timecode-triggered scene playback
        sceneCheckTimecodeTrigger();
        return;
    }

    // --- Art-Net Trigger (opcode 0x9900) ---
    if (op == ARTNET_OP_TRIGGER) {
        // Layout: [8]id [9]op [10]flags [11]key [12]subkey
        uint8_t key    = p[11];
        uint8_t subkey = p[12];
        // Key 0-255; treat key as a scene index for simple triggering.
        // If key is 0xFF, use subkey as scene index.
        int sceneIdx = (key == 0xFF) ? (int)subkey : (int)key;
        if (sceneIdx >= 0 && sceneIdx < MAX_SCENES) {
            sceneTriggerPlay(sceneIdx, 0);
            Serial.printf("[ART] Trigger -> scene %d\n", sceneIdx);
        }
        return;
    }

    if (op == ARTNET_OP_NZS) {
        if (n < 19 || cfg.protocol == 1) return;
        uint16_t universe = p[14] | (p[15] << 8);
        uint16_t length   = (p[16] << 8) | p[17];
        if (length > 512) length = 512;
        if (18 + length > n) length = n - 18;
        uint8_t startCode = p[18];
        uint8_t priority  = (n >= 60) ? p[59] : DEFAULT_PRIORITY;

        uint8_t frame[DMX_PACKET_SIZE];
        memcpy(frame, p + 18, length);
        routeFrameNzs((int)universe, frame, length, startCode, ip, priority);
        return;
    }
}

void commitArtSyncStaged() {
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (!cfg.outputs[i].enabled || !dmxStagedValid[i]) continue;
        dmxBufWriteBegin(i);
        memcpy(&dmxBuffers[i].data[1], dmxStaged[i], dmxStagedLen[i]);
        if (dmxStagedLen[i] < 512) memset(&dmxBuffers[i].data[1 + dmxStagedLen[i]], 0, 512 - dmxStagedLen[i]);
        dmxBufWriteEnd(i);
        updateSender(0, 0, (int16_t)portAddress(cfg.outputs[i]), DEFAULT_PRIORITY, dmxStaged[i], dmxStagedLen[i]);
        dmxStagedValid[i] = false;
        mergeOutput(i);
    }
}
