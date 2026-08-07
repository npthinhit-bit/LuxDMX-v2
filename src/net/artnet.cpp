#include "artnet.h"
#include "config_schema.h"
#include "frame_router.h"
#include "network.h"
#include "ethernet.h"
#include "rdm_engine.h"   // rdmOut, rdmLineForOut
#include "output_init.h"
#include "sender_tracker.h"
#include <Arduino.h>
#include <esp_wifi.h>
#include <fcntl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <lwip/sockets.h>

// ---- module config ----
static int      g_artSock       = -1;
static bool     g_artRdmReady   = false;
static uint32_t g_nodeIp        = 0;
static uint8_t  g_nodeMac[6]    = {0};
static bool     g_artRdmEnabled = true;
static uint16_t g_artPolls      = 0;
uint8_t                g_bqPolicy     = 4;   // 0..3 severity, 4 = disabled (default)
volatile bool   g_bqDirty     = false;
volatile bool   g_artCfgDirty = false;

// Forward declarations (full implementations to be split into artnet_rdm module)
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
    // Placeholder: full response queue drained in the artnet_rdm module.
}

static void artHandlePacket(const uint8_t* p, int n, uint32_t ip) {
    uint16_t op = p[8] | (p[9] << 8);
    if (op == ARTNET_OP_DMX) {
        if (n < 18 || cfg.protocol == 1) return;
        uint16_t universe = p[14] | (p[15] << 8);
        uint16_t length   = (p[16] << 8) | p[17];
        if (length > 512) length = 512;
        if (18 + length > n) length = n - 18;
        routeFrame((int)universe, p + 18, length, ip, 0, DEFAULT_PRIORITY);
        return;
    }
    // RDM bridge opcodes (poll, address, tod, rdm) handled by artnet_rdm module.
}
