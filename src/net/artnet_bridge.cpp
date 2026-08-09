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
    const char* shortName = "LuxDMX";
    const char* nodeName = "LuxDMX Professional";
    uint8_t reply[130];
    memset(reply, 0, sizeof(reply));
    memcpy(reply + 0, ARTNET_ID, 8);
    reply[8]  = 0x00;     // opcode lo (0x2100 for PollReply)
    reply[9]  = 0x21;
    reply[10] = 14;       // protocol version hi (14)
    reply[11] = 0;        // protocol version lo (14.0)
    reply[12] = 0;        // root device (not a media device)
    reply[13] = MAX_OUTPUTS;  // port count lo
    reply[14] = MAX_OUTPUTS;  // port count hi
    reply[16] = 0x02;     // OEM lo (0x02 = ESTA vendor-less, temporary)
    reply[17] = 0x00;     // OEM hi
    reply[18] = 0;        // ubea
    // Status1: bit 4 = port type DMX, bit 3 = indicator (0=normal boot),
    // bits 0-2 = product category (0=generic)
    reply[19] = 0x08;     // status1: port type = DMX, normal boot
    // Port types: bit 7 = output, bit 6 = input, bits 0-3 = protocol (0=DMX)
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        reply[20 + i] = cfg.outputs[i].enabled ? 0x80 : 0x00;  // DMX output (or disabled)
    }
    // Good input (4 bytes, bit-field — all 0 = not implemented)
    memset(reply + 24, 0, 4);
    // Port addresses (1 byte per port, lower 4 bits = universe for this port)
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        reply[28 + i] = (uint8_t)(cfg.outputs[i].universe & 0x0F);  // port universe
    }
    // Input ports (all 0 = no input)
    memset(reply + 32, 0, 4);
    // Good output (bit-field — 0 = not implemented)
    memset(reply + 36, 0, 4);
    // Spare (bytes 40-43)
    memset(reply + 40, 0, 4);

    // Net (0-127) at byte 34, subnet (4 bits) at byte 35 lower nibble
    reply[34] = (uint8_t)cfg.outputs[0].net;
    reply[35] = (uint8_t)(cfg.outputs[0].subnet << 4);

    for (int i = 0; i < 16; i++) reply[48 + i] = shortName[i] ? shortName[i] : ' ';
    for (int i = 0; i < 16; i++) reply[64 + i] = nodeName[i] ? nodeName[i] : ' ';

    uint32_t ipLe = g_nodeIp;
    memcpy(reply + 74, &ipLe, 4);
    reply[78] = (uint8_t)(ARTNET_PORT & 0xFF);
    reply[79] = (uint8_t)((ARTNET_PORT >> 8) & 0xFF);
    reply[80] = MAX_OUTPUTS;        // num ports hi
    reply[81] = 0;                  // num ports lo
    // Status2: 0x00 = no extended features, RDM not active
    reply[82] = cfg.artnetRdm ? 0x80 : 0x00;  // bit 7 = RDM support
    // Extended (4 bytes): 0 = nothing pending
    memset(reply + 83, 0, 4);

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
            // Art-Address: 'A' command sets the low 4 bits of universe (0-15).
            // Full 15-bit addressing uses separate Net ('N') and Subnet ('S') commands.
            for (int i = 0; i < MAX_OUTPUTS; i++) {
                cfg.outputs[i].universe = address & 0x0F;
            }
            break;
        case 'U':  // Unlock: commit pending changes
            g_artCfgDirty = true;
            saveConfig();
            break;
        case 'N':  // Net: set net switch on addressed ports
            for (int i = 0; i < MAX_OUTPUTS; i++) {
                if (address & (1 << i)) cfg.outputs[i].net = dataVal & 0x7F;
            }
            g_artCfgDirty = true;
            break;
        case 'S':  // Subnet: set subnet on addressed ports
            for (int i = 0; i < MAX_OUTPUTS; i++) {
                if (address & (1 << i)) cfg.outputs[i].subnet = dataVal & 0x0F;
            }
            g_artCfgDirty = true;
            break;
        case 'I':  // Reset micro device to defaults (factory reset)
            saveConfig();  // persist any pending changes before reset
            pendingFactoryReset = true;
            pendingRebootAt = millis();
            break;
        case 'M':  // Merge mode: 0=HTB, 1=HTP, 2=LTP
            for (int i = 0; i < MAX_OUTPUTS; i++) {
                if (address & (1 << i)) {
                    if (dataVal <= 2) cfg.outputs[i].mergeMode = dataVal;
                }
            }
            g_artCfgDirty = true;
            break;
        case 'V':  // Set the device's own port type (0=Input, 1=Output)
            // Bit 7 of dataVal indicates the port type to set; address is bitmask
            for (int i = 0; i < MAX_OUTPUTS; i++) {
                if (address & (1 << i)) {
                    // Not modifying port type at runtime (configured via web UI);
                    // acknowledge but no action needed for type field
                }
            }
            break;
        case 'Q':  // Set port universe (4-bit) on addressed ports
            for (int i = 0; i < MAX_OUTPUTS; i++) {
                if (address & (1 << i)) cfg.outputs[i].universe = dataVal & 0x0F;
            }
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

static void handleArtTodRequest(const uint8_t* p, int n, uint32_t ip) {
    // ArtTodRequest: query TOD for one or more ports.
    // Layout: id(8) + opCode(2) + ver(1) + FAEver(1) + flags(1) + spare(1) +
    //         port(1) + spare(9) + port bitmaps (32 bytes, one bit per port)
    if (n < 48) return;
    int portIdx = p[18];  // requested port index (0-based)
    if (portIdx < 0 || portIdx >= MAX_OUTPUTS) return;
    if (!cfg.outputs[portIdx].enabled) return;

    // Build ArtTodData response with discovered RDM devices from this port's RDM line
    uint8_t reply[576];
    memset(reply, 0, sizeof(reply));
    memcpy(reply, ARTNET_ID, 8);
    reply[8]  = 0x00;     // ArtTodData opcode (0x8100) little-endian
    reply[9]  = 0x81;
    reply[10] = 14;       // protocol version lo
    reply[11] = 0;        // protocol version hi
    reply[12] = portIdx;  // port address
    reply[13] = 0;        // command (0 = current TOD)
    reply[14] = 0;        // data — will be filled with UID count

    // Collect UIDs for this port from the RDM discovery data
    // The TOD array in stats.h holds all discovered UIDs regardless of port.
    // In a full implementation, we'd filter by port; for now, include all.
    uint8_t uidCount = 0;
    int offset = 15;
    for (int i = 0; i < RDM_TOD_MAX && uidCount < 200; i++) {
        if (offset + 6 > (int)sizeof(reply)) break;
        reply[offset++] = (rdmTod[i].man_id >> 8) & 0xFF;
        reply[offset++] = rdmTod[i].man_id & 0xFF;
        reply[offset++] = (rdmTod[i].dev_id >> 24) & 0xFF;
        reply[offset++] = (rdmTod[i].dev_id >> 16) & 0xFF;
        reply[offset++] = (rdmTod[i].dev_id >> 8) & 0xFF;
        reply[offset++] = rdmTod[i].dev_id & 0xFF;
        uidCount++;
    }
    reply[14] = uidCount;

    struct sockaddr_in dst = {};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(ARTNET_PORT);
    dst.sin_addr.s_addr = ip;
    lwip_sendto(g_artSock, reply, offset, 0, (struct sockaddr*)&dst, sizeof(dst));
}

// ArtTodControl: commands to manage the TOD cache (flush/discovery control).
// This is a minimal implementation that acknowledges the command.
static void handleArtTodControl(const uint8_t* p, int n, uint32_t ip) {
    (void)p; (void)n; (void)ip;
    // ArtTodControl allows a controller to instruct the node to perform
    // discovery or flush the TOD cache. We acknowledge by doing nothing
    // — the TOD is maintained by the local RDM discovery sweep.
    // In a full implementation, command 0x01 = flush TOD cache.
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
        case ARTNET_OP_TODDATA:     break;  // we don't receive TOD data
        case ARTNET_OP_TODCONTROL:  handleArtTodControl(p, n, ip); break;
        case ARTNET_OP_RDM:         handleArtRdm(p, n, ip); break;
        case ARTNET_OP_SYNC:
            break;
        default: break;
    }
}
