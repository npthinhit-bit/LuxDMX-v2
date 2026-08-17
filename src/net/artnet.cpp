#include "artnet.h"
#include "art_pkt_queue.h"
#include "art_rdm_resp_queue.h"
#include "config_schema.h"
#include "dmx_buffer.h"
#include "ethernet.h"
#include "frame_router.h"
#include "merge_engine.h"
#include "net_state.h"
#include "output_init.h"
#include "rdm_engine.h"
#include "scene_engine.h"
#include "sender_tracker.h"
#include <Arduino.h>
#include <esp_wifi.h>
#include <fcntl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>

static ArtNetState g_artNet;

ArtNetState& artNet()
{
    return g_artNet;
}

static void artHandlePacket(const uint8_t* p, int n, uint32_t ip);
static void sendTimecode();

void artRdmInit()
{
    artPktQueueInit();
    artRdmRespQueueInit();
    g_artNet.nodeIp = (uint32_t)netLocalIP();
    uint8_t m[6];
    esp_wifi_get_mac(WIFI_IF_STA, m);
    memcpy(g_artNet.nodeMac, m, 6);
    g_artNet.artRdmEnabled = cfg.artnetRdm;
    g_artNet.timecodeSend  = cfg.timecodeSend;
    g_artNet.timecodeType  = cfg.timecodeType;
    g_artNet.timecodeFps   = cfg.timecodeFps;
    g_artNet.artSock       = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_artNet.artSock >= 0)
    {
        int one = 1;
        lwip_setsockopt(g_artNet.artSock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        lwip_setsockopt(g_artNet.artSock, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
        // QoS/DSCP marking for time-sensitive DMX traffic
        if (cfg.dscpEnabled && cfg.dscpDmx >= 0 && cfg.dscpDmx <= 63)
        {
            int tos = (cfg.dscpDmx & 0x3F) << 2;  // DSCP in upper 6 bits of TOS
            lwip_setsockopt(g_artNet.artSock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
        }
        struct sockaddr_in a = {};
        a.sin_family         = AF_INET;
        a.sin_port           = htons(ARTNET_PORT);
        a.sin_addr.s_addr    = INADDR_ANY;
        lwip_bind(g_artNet.artSock, (struct sockaddr*)&a, sizeof(a));
        int fl = lwip_fcntl(g_artNet.artSock, F_GETFL, 0);
        lwip_fcntl(g_artNet.artSock, F_SETFL, fl | O_NONBLOCK);
        g_artNet.artRdmReady = true;
    }
    else
    {
        Serial.println("[ART] socket() failed");
    }
    Serial.printf("[ART] Art-Net up on :%d (RDM %s)\n", ARTNET_PORT, g_artNet.artRdmEnabled ? "on" : "off");
}

void artRdmPollRx()
{
    if (!g_artNet.artRdmReady || g_artNet.artSock < 0)
        return;
    static uint8_t buf[640];
    // Producer: recv at most 8 packets/tick (was 64) and enqueue parsed Art-Net.
    for (int k = 0; k < 8; k++)
    {
        struct sockaddr_in src;
        socklen_t          sl = sizeof(src);
        int                n  = lwip_recvfrom(g_artNet.artSock, buf, sizeof(buf), 0, (struct sockaddr*)&src, &sl);
        if (n <= 0)
            return;
        if (n >= 12 && memcmp(buf, ARTNET_ID, 8) == 0)
        {
            ArtPkt p;
            memset(&p, 0, sizeof(p));
            if (n > (int)ART_PKT_MAX)
                n = (int)ART_PKT_MAX;
            memcpy(p.data, buf, n);
            p.len   = (uint16_t)n;
            p.srcIp = (uint32_t)src.sin_addr.s_addr;
            artPktPush(p);  // drop on full (back-pressure under burst)
        }
    }
    // Send TimeCode if enabled
    sendTimecode();
}

// Consumer: dispatch all enqueued Art-Net packets. Called from netRxTask (core 0)
// right after artRdmPollRx(). Same task as the producer, so no cross-core locks.
void artPktDispatchAll()
{
    ArtPkt p;
    while (artPktPop(p))
    {
        artHandlePacket(p.data, p.len, p.srcIp);
    }
}

void artRdmDrainResponses()
{
    // Drain the core-1 RDM response ring and send each ArtRdm reply (opcode 0x8300)
    // back to the original requester over the 6454 socket. Core 1 pushes via
    // artRdmPushResponse(); this runs on core 0 in netRxTask. No blocking, no
    // per-packet heap: just a bounded drain of the ring.
    ArtRdmResp r;
    while (artRdmRespPop(r))
    {
        if (g_artNet.artSock < 0 || r.len == 0)
            continue;
        uint8_t reply[576];
        memset(reply, 0, sizeof(reply));
        memcpy(reply, ARTNET_ID, 8);
        reply[8]         = 0x00;
        reply[9]         = 0x83;  // ArtRdm opcode (0x8300) little-endian
        reply[10]        = 14;    // protocol version hi
        uint16_t respLen = r.len;
        if (respLen > 256)
            respLen = 256;
        reply[17] = r.data[0];
        if (respLen > 1)
            memcpy(reply + 18, r.data + 1, respLen - 1);
        struct sockaddr_in dst = {};
        dst.sin_family         = AF_INET;
        dst.sin_port           = htons(ARTNET_PORT);
        dst.sin_addr.s_addr    = r.destIp;
        lwip_sendto(g_artNet.artSock, reply, 18 + respLen, 0, (struct sockaddr*)&dst, sizeof(dst));
    }
}

// --- Art-Net TimeCode send ---
static void sendTimecode()
{
    if (!g_artNet.timecodeSend || g_artNet.artSock < 0)
        return;
    uint32_t now = millis();
    if (now - g_artNet.tcLastSendMs < 1000 / g_artNet.timecodeFps)
        return;
    g_artNet.tcLastSendMs = now;

    // Build ArtTimeCode packet (opcode 0x9700, 13 bytes total)
    uint8_t pkt[64];
    memset(pkt, 0, sizeof(pkt));
    memcpy(pkt, ARTNET_ID, 8);
    pkt[8]  = ARTNET_OP_TIMECODE & 0xFF;  // opcode (little-endian)
    pkt[9]  = (ARTNET_OP_TIMECODE >> 8) & 0xFF;
    pkt[10] = 0;  // flags
    // Data bytes 11-14: type(3b | hour 3b | minute 8b | second 8b | frame 8b)
    pkt[11] = ((g_artNet.timecode.type & 0x07) << 5) | ((g_artNet.timecode.hour & 0x07) << 2);
    pkt[12] = g_artNet.timecode.minute;
    pkt[13] = g_artNet.timecode.second;
    pkt[14] = g_artNet.timecode.frame;

    struct sockaddr_in dst = {};
    dst.sin_family         = AF_INET;
    dst.sin_port           = htons(ARTNET_PORT);
    dst.sin_addr.s_addr    = 0xFFFFFFFFu;  // broadcast
    lwip_sendto(g_artNet.artSock, pkt, 15, 0, (struct sockaddr*)&dst, sizeof(dst));
}

static void artHandlePacket(const uint8_t* p, int n, uint32_t ip)
{
    uint16_t op = p[8] | (p[9] << 8);

    if (op == ARTNET_OP_SYNC)
    {
        g_artNet.syncMode = false;  // SYNC commits the staged frames and returns to immediate mode
        commitArtSyncStaged();
        artnetBridgeDispatch(op, p, n, ip);
        return;
    }

    if (g_artNet.syncMode)
    {
        uint32_t now = millis();
        if (now - g_artNet.syncLastMs > ARTSYNC_TIMEOUT_MS)
        {
            Serial.println("[ART] ArtSync timeout, falling back to immediate");
            g_artNet.syncMode = false;
        }
    }

    if (op == ARTNET_OP_POLL || op == ARTNET_OP_ADDRESS || op == ARTNET_OP_IPPROG || op == ARTNET_OP_TODREQUEST ||
        op == ARTNET_OP_RDM)
    {
        artnetBridgeDispatch(op, p, n, ip);
        return;
    }

    if (op == ARTNET_OP_DMX)
    {
        if (n < 18 || cfg.protocol == 1)
            return;
        uint16_t universe = p[14] | (p[15] << 8);
        uint16_t length   = (p[16] << 8) | p[17];
        if (length > 512)
            length = 512;
        if (18 + length > n)
            length = n - 18;
        uint8_t priority = (n >= 60) ? p[59] : DEFAULT_PRIORITY;

        if (g_artNet.syncMode)
        {
            for (int i = 0; i < MAX_OUTPUTS; i++)
            {
                if (!cfg.outputs[i].enabled || portAddress(cfg.outputs[i]) != universe)
                    continue;
                memcpy(dmxBufferState().staged[i], p + 18, length);
                dmxBufferState().stagedLen[i]   = length;
                dmxBufferState().stagedValid[i] = true;
            }
            updateSender(ip, 0, (int16_t)universe, priority, p + 18, length);
            return;
        }
        routeFrame((int)universe, p + 18, length, ip, 0, priority);
        return;
    }

    // --- Art-Net TimeCode (opcode 0x9700) ---
    if (op == ARTNET_OP_TIMECODE && n >= 13)
    {
        // Layout: [8]id [9]op [10]flags [11]type [12]hour [13]min [14]sec [15]frame
        g_artNet.timecode.type   = (p[11] >> 5) & 0x07;
        g_artNet.timecode.hour   = (p[11] >> 2) & 0x07;
        g_artNet.timecode.minute = p[12];
        g_artNet.timecode.second = p[13];
        g_artNet.timecode.frame  = p[14];
        g_artNet.timecodeValid   = true;
        // Timecode-triggered scene playback
        sceneCheckTimecodeTrigger();
        return;
    }

    // --- Art-Net Trigger (opcode 0x9900) ---
    if (op == ARTNET_OP_TRIGGER)
    {
        // Layout: [8]id [9]op [10]flags [11]key [12]subkey
        uint8_t key    = p[11];
        uint8_t subkey = p[12];
        // Key 0-255; treat key as a scene index for simple triggering.
        // If key is 0xFF, use subkey as scene index.
        int sceneIdx = (key == 0xFF) ? (int)subkey : (int)key;
        if (sceneIdx >= 0 && sceneIdx < MAX_SCENES)
        {
            sceneTriggerPlay(sceneIdx, 0);
            Serial.printf("[ART] Trigger -> scene %d\n", sceneIdx);
        }
        return;
    }

    if (op == ARTNET_OP_NZS)
    {
        if (n < 19 || cfg.protocol == 1)
            return;
        uint16_t universe = p[14] | (p[15] << 8);
        uint16_t length   = (p[16] << 8) | p[17];
        if (length > 512)
            length = 512;
        if (18 + length > n)
            length = n - 18;
        uint8_t startCode = p[18];
        uint8_t priority  = (n >= 60) ? p[59] : DEFAULT_PRIORITY;

        uint8_t frame[DMX_PACKET_SIZE];
        memcpy(frame, p + 18, length);
        routeFrameNzs((int)universe, frame, length, startCode, ip, priority);
        return;
    }
}

void commitArtSyncStaged()
{
    for (int i = 0; i < MAX_OUTPUTS; i++)
    {
        if (!cfg.outputs[i].enabled || !dmxBufferState().stagedValid[i])
            continue;
        dmxBufWriteBegin(i);
        memcpy(&dmxBufferState().buffers[i].data[1], dmxBufferState().staged[i], dmxBufferState().stagedLen[i]);
        if (dmxBufferState().stagedLen[i] < 512)
            memset(&dmxBufferState().buffers[i].data[1 + dmxBufferState().stagedLen[i]], 0,
                   512 - dmxBufferState().stagedLen[i]);
        dmxBufWriteEnd(i);
        updateSender(0, 0, (int16_t)portAddress(cfg.outputs[i]), DEFAULT_PRIORITY, dmxBufferState().staged[i],
                     dmxBufferState().stagedLen[i]);
        dmxBufferState().stagedValid[i] = false;
        mergeOutput(i);
    }
}
