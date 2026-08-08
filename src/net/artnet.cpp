#include "artnet.h"
#include "config_schema.h"
#include "frame_router.h"
#include "merge_engine.h"
#include "net_state.h"
#include "ethernet.h"
#include "rdm_engine.h"
#include "output_init.h"
#include "sender_tracker.h"
#include "dmx_buffer.h"
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

static void artHandlePacket(const uint8_t* p, int n, uint32_t ip);

void artRdmInit() {
    g_nodeIp = (uint32_t)netLocalIP();
    uint8_t m[6];
    esp_wifi_get_mac(WIFI_IF_STA, m);
    memcpy(g_nodeMac, m, 6);
    g_artRdmEnabled = cfg.artnetRdm;
    g_artSock = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_artSock >= 0) {
        int one = 1;
        lwip_setsockopt(g_artSock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        lwip_setsockopt(g_artSock, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
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
    for (int k = 0; k < 64; k++) {
        struct sockaddr_in src; socklen_t sl = sizeof(src);
        int n = lwip_recvfrom(g_artSock, buf, sizeof(buf), 0, (struct sockaddr*)&src, &sl);
        if (n <= 0) return;
        if (n >= 12 && memcmp(buf, ARTNET_ID, 8) == 0)
            artHandlePacket(buf, n, (uint32_t)src.sin_addr.s_addr);
    }
}

void artRdmDrainResponses() {
    // Response queue drained — placeholder for queued ArtRdm/ArtTod replies.
    // In a full implementation, the DMX task would push responses to a queue
    // and this function would send them over the Art-Net socket.
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
