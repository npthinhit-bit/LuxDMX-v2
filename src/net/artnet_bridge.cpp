#include "artnet.h"
#include "config_schema.h"
#include "config_core.h"
#include "frame_router.h"
#include "firmware_version.h"
#include "sys_platform.h"
#include "net_state.h"
#include "dmx_buffer.h"
#include "sender_tracker.h"
#include "output_init.h"
#include "rdm_engine.h"
#include "rdm_disc.h"
#include "stats.h"
#include <Arduino.h>
#include <esp_wifi.h>
#include <string.h>
#include <lwip/sockets.h>

extern int g_artSock;
extern uint32_t g_nodeIp;
extern uint8_t  g_nodeMac[6];
extern uint16_t g_artPolls;

static void sendArtPollReply(uint32_t ip) {
    uint8_t reply[130];
    memset(reply, 0, sizeof(reply));
    memcpy(reply + 0, ARTNET_ID, 8);
    reply[8]  = 0x00;     // opcode lo (0x2100 for PollReply)
    reply[9]  = 0x21;
    reply[10] = 14;       // protocol version hi
    reply[11] = 0;        // protocol version lo (14.0)
    reply[12] = 0;        // 0 = root device
    reply[13] = MAX_OUTPUTS;  // port count lo
    reply[14] = MAX_OUTPUTS;  // port count hi
    reply[16] = 0x02;     // OEM lo (0x02 = ESTA vendor-less, temporary)
    reply[17] = 0x00;     // OEM hi
    reply[18] = 0;        // ubea
    reply[19] = 0;        // status 1
    reply[20] = 0x80;     // port 0-3 type: DMX output
    reply[21] = 0x80;
    reply[22] = 0x80;
    reply[23] = 0x80;
    reply[28] = 0x01;     // output port 0
    reply[29] = 0x02;     // output port 1
    reply[30] = 0x03;     // output port 2
    reply[31] = 0x04;     // output port 3

    // Net (0-127) at byte 34, subnet (lower nibble) at byte 35
    reply[34] = (uint8_t)cfg.outputs[0].net;
    reply[35] = (uint8_t)(cfg.outputs[0].subnet << 4);

    const char* shortName = "LuxDMX";
    for (int i = 0; i < 16; i++) reply[48 + i] = shortName[i] ? shortName[i] : ' ';

    const char* nodeName = "LuxDMX Professional";
    for (int i = 0; i < 16; i++) reply[66 + i] = nodeName[i] ? nodeName[i] : ' ';

    uint32_t ipLe = g_nodeIp;
    memcpy(reply + 74, &ipLe, 4);
    reply[78] = (uint8_t)(ARTNET_PORT & 0xFF);
    reply[79] = (uint8_t)((ARTNET_PORT >> 8) & 0xFF);
    reply[80] = 1;
    reply[81] = 0;

    struct sockaddr_in dst = {};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(ARTNET_PORT);
    dst.sin_addr.s_addr = ip;

    lwip_sendto(g_artSock, reply, sizeof(reply), 0, (struct sockaddr*)&dst, sizeof(dst));
}

static void handleArtAddress(const uint8_t* p, int n, uint32_t ip) {
    (void)ip;
    if (n < 20) return;
    uint8_t command = p[18];
    uint8_t address = p[19];
    uint8_t dataVal = (n > 20) ? p[20] : 0;
    switch (command) {
        case 'A':  // All: global addressing — set universe on all ports
            for (int i = 0; i < MAX_OUTPUTS; i++) {
                cfg.outputs[i].universe = address & 0x0F;
            }
            break;
        case 'U':  // Unlock: commit pending changes
            g_artCfgDirty = true;
            saveConfig();
            break;
        case 'N':  // Net: set net switch on addressed ports
            cfg.outputs[address].net = dataVal;
            g_artCfgDirty = true;
            break;
        case 'S':  // Subnet: set subnet on addressed ports
            cfg.outputs[address].subnet = dataVal & 0x0F;
            g_artCfgDirty = true;
            break;
        case 'T':  // Test fade
            break;
        default:
            break;
    }
}

static void handleArtIpProg(const uint8_t* /*p*/, int /*n*/, uint32_t /*ip*/) {
    g_artCfgDirty = true;
}

static void handleArtTodRequest(const uint8_t* /*p*/, int /*n*/, uint32_t /*ip*/) {
}

static void handleArtRdm(const uint8_t* p, int n, uint32_t ip) {
    (void)ip;
    if (!cfg.artnetRdm || n < 24) return;
    // ArtRdm: command at offset 18, parameter data follows.
    // The RDM message starts at offset 18 in the ArtRdm packet (after the
    // 8-byte ID + 2 opcode + 11-byte header = 21 bytes), but the ArtRdm
    // format has: id(8) + opCode(2) + Ver(1) + FAEver(1) + Flags(1) +
    // RDM commands(1) + Trans# (1) + FAEs(1) + RDM data (n-24 bytes)
    // The RDM packet (with start code) starts at offset 18.
    rdmRmtSelect(rdmOut);
    uint8_t respBuf[256];
    int respLen = rdmRmtRawRelay(p + 18, n - 18, respBuf, sizeof(respBuf));
    if (respLen > 0) {
        // Build ArtRdm response and send back to the controller.
        uint8_t reply[576];
        memset(reply, 0, sizeof(reply));
        memcpy(reply, ARTNET_ID, 8);
        reply[8] = 0x00; reply[9] = 0x83;  // ArtRdm opcode (0x8300) little-endian
        reply[10] = 14;
        reply[17] = respBuf[0];  // RDM start code
        memcpy(reply + 18, respBuf + 1, respLen - 1);
        struct sockaddr_in dst = {};
        dst.sin_family = AF_INET;
        dst.sin_port = htons(ARTNET_PORT);
        dst.sin_addr.s_addr = ip;
        lwip_sendto(g_artSock, reply, 18 + respLen, 0, (struct sockaddr*)&dst, sizeof(dst));
    }
}

static void handleArtPoll(const uint8_t* p, int /*n*/, uint32_t ip) {
    (void)p;
    g_artPolls++;
    sendArtPollReply(ip);
}

void artnetBridgeDispatch(uint16_t op, const uint8_t* p, int n, uint32_t ip) {
    switch (op) {
        case ARTNET_OP_POLL:        handleArtPoll(p, n, ip); break;
        case ARTNET_OP_POLLREPLY:   break;
        case ARTNET_OP_ADDRESS:     handleArtAddress(p, n, ip); break;
        case ARTNET_OP_IPPROG:      handleArtIpProg(p, n, ip); break;
        case ARTNET_OP_TODREQUEST:  handleArtTodRequest(p, n, ip); break;
        case ARTNET_OP_RDM:         handleArtRdm(p, n, ip); break;
        case ARTNET_OP_SYNC:
            break;
        default: break;
    }
}
