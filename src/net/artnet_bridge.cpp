#include "artnet.h"
#include "config_core.h"
#include "config_schema.h"
#include "dmx_buffer.h"
#include "firmware_version.h"
#include "frame_router.h"
#include "net_state.h"
#include "output_init.h"
#include "rdm_disc.h"
#include "rdm_engine.h"
#include "rdm_task.h"
#include "sender_tracker.h"
#include "stats.h"
#include "sys_platform.h"
#include <Arduino.h>
#include <esp_wifi.h>
#include <lwip/sockets.h>
#include <string.h>

static void sendArtPollReply(uint32_t ip)
{
    uint8_t reply[240];
    memset(reply, 0, sizeof(reply));
    memcpy(reply + 0, ARTNET_ID, 8);
    reply[8]  = 0x21;
    reply[9]  = 0x00;
    reply[10] = 14;
    reply[11] = 0;
    reply[12] = 1;
    reply[13] = 0;
    reply[14] = MAX_OUTPUTS;
    reply[15] = 0;
    reply[16] = 0x01;
    reply[17] = 0x00;

    // ESTA manufacturer code - placeholder, replace with a registered ESTA code
    reply[18] = 0x00;
    reply[19] = 0x00;
    reply[20] = 0x00;
    reply[21] = 0x00;

    const char* shortName = "LuxDMX V2";
    for (int i = 0; i < 18; i++)
        reply[22 + i] = shortName[i] ? shortName[i] : 0x20;

    const char* longName = "LuxDMX V2 Art-Net/sACN to DMX Gateway";
    for (int i = 0; i < 34; i++)
        reply[40 + i] = longName[i] ? longName[i] : 0x20;

    const char* nodeReport = "2000:0000:0001:0100 - LuxDMX V2 Ready";
    for (int i = 0; i < 64; i++)
        reply[74 + i] = nodeReport[i] ? nodeReport[i] : 0x20;

    reply[140] = 0x00;
    reply[141] = 0x00;

    reply[144] = 0x00;
    reply[145] = 0x00;

    // SwIn[4] (bytes 148-151): input port switch style — 0 = DMX. No inputs on LuxDMX.
    for (int i = 0; i < MAX_OUTPUTS; i++)
        reply[148 + i] = 0x00;

    // SwOut[4] (bytes 152-155): output port switch style — 0 = DMX
    for (int i = 0; i < MAX_OUTPUTS; i++)
        reply[152 + i] = 0x00;

    IPAddress localIp = netLocalIP();
    uint32_t  ipVal   = (uint32_t)localIp;
    memcpy(reply + 156, &ipVal, 4);

    IPAddress sn    = netSubnetMask();
    uint32_t  snVal = (uint32_t)sn;
    memcpy(reply + 160, &snVal, 4);

    IPAddress gw    = netGatewayIP();
    uint32_t  gwVal = (uint32_t)gw;
    memcpy(reply + 164, &gwVal, 4);

    for (int i = 0; i < MAX_OUTPUTS; i++)
        reply[168 + i] = 0x80;

    for (int i = 0; i < MAX_OUTPUTS; i++)
        reply[172 + i] = cfg.outputs[i].enabled ? 0x80 : 0x00;

    memcpy(reply + 176, artNet().nodeMac, 6);

    struct sockaddr_in dst = {};
    dst.sin_family         = AF_INET;
    dst.sin_port           = htons(ARTNET_PORT);
    dst.sin_addr.s_addr    = ip;

    lwip_sendto(artNet().artSock, reply, sizeof(reply), 0, (struct sockaddr*)&dst, sizeof(dst));
}

static void handleArtAddress(const uint8_t* p, int n, uint32_t ip)
{
    (void)ip;
    if (n < 20)
        return;
    uint8_t command = p[18];
    uint8_t address = p[19];
    uint8_t dataVal = (n > 20) ? p[20] : 0;
    switch (command)
    {
    case 'A':  // All: global addressing â€” set universe on all ports
        for (int i = 0; i < MAX_OUTPUTS; i++)
        {
            cfg.outputs[i].universe = address & 0x0F;
        }
        break;
    case 'U':  // Unlock: commit pending changes
        artNet().artCfgDirty = true;
        saveConfig();
        break;
    case 'N':  // Net: set net switch on addressed ports
        cfg.outputs[address].net = dataVal;
        artNet().artCfgDirty     = true;
        break;
    case 'S':  // Subnet: set subnet on addressed ports
        cfg.outputs[address].subnet = dataVal & 0x0F;
        artNet().artCfgDirty        = true;
        break;
    case 'T':  // Test fade
        break;
    default:
        break;
    }
}

static void handleArtIpProg(const uint8_t* /*p*/, int /*n*/, uint32_t ip)
{
    if (!cfg.ipProg)
        return;
    if (!netIsLocalSubnet(ip))
    {
        Serial.printf("[ART] ArtIpProg from non-local subnet %08x, ignoring\n", ip);
        return;
    }
    artNet().artCfgDirty = true;
}

static void handleArtTodRequest(const uint8_t* /*p*/, int /*n*/, uint32_t /*ip*/) {}

static void handleArtRdm(const uint8_t* p, int n, uint32_t ip)
{
    if (!cfg.artnetRdm || n < 24)
        return;
    if (artNet().artSock < 0)
        return;
    // The RDM message starts at offset 18 in the ArtRdm packet (after the
    // 8-byte ID + 2 opcode + 11-byte header = 21 bytes), but the ArtRdm
    // format has: id(8) + opCode(2) + Ver(1) + FAEver(1) + Flags(1) +
    // RDM commands(1) + Trans# (1) + FAEs(1) + RDM data (n-24 bytes)
    // The RDM packet (with start code) starts at offset 18.
    // Enqueue to the core-1 DMX task WITHOUT blocking core 0. The old path called
    // rdmRmtRawRelay() synchronously here, stalling WiFi/AsyncTCP for up to ~5s per
    // RDM transaction. Core 1 now runs RMT TX/RX and pushes the reply to the
    // Art-Net response ring; netRxTask drains+ sends it via artRdmDrainResponses().
    // Line selection (rdmRmtSelect) is deferred to the core-1 task handler to avoid
    // a race where core 0 could overwrite the line mid-transaction.
    uint16_t reqLen = (uint16_t)(n - 18);
    if (reqLen > 256)
        reqLen = 256;
    int line = (rdmOut >= 0) ? rdmLineForOut[rdmOut] : -1;
    rdmArtRawRelayEnqueue(p + 18, reqLen, ip, line);
}

static void handleArtPoll(const uint8_t* p, int /*n*/, uint32_t ip)
{
    (void)p;
    artNet().artPolls++;
    sendArtPollReply(ip);
}

void artnetBridgeDispatch(uint16_t op, const uint8_t* p, int n, uint32_t ip)
{
    switch (op)
    {
    case ARTNET_OP_POLL:
        handleArtPoll(p, n, ip);
        break;
    case ARTNET_OP_POLLREPLY:
        break;
    case ARTNET_OP_ADDRESS:
        handleArtAddress(p, n, ip);
        break;
    case ARTNET_OP_IPPROG:
        handleArtIpProg(p, n, ip);
        break;
    case ARTNET_OP_TODREQUEST:
        handleArtTodRequest(p, n, ip);
        break;
    case ARTNET_OP_RDM:
        handleArtRdm(p, n, ip);
        break;
    case ARTNET_OP_SYNC:
        break;
    default:
        break;
    }
}
