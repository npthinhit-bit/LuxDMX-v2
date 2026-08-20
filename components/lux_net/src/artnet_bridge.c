/*
 * spec 17 - Art-Net Bridge dispatch
 *
 * Handles Art-Net opcodes forwarded by artnet_dispatch_packet:
 *   0x2000 ArtPoll, 0x6000 ArtAddress, 0x8300 ArtRDM.
 * Unknown opcodes are dropped.
 */
#include "artnet_bridge.h"
#include "artnet.h"
#include "config_engine.h"
#include "logger.h"
#include "esp_wifi.h"
#include "wifi_manager.h"
#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>

static const char* TAG = "artBridge";

static void fillNameField(uint8_t* dst, const char* src, size_t len)
{
    memset(dst, ' ', len);
    size_t slen = strlen(src);
    if (slen > len) {
        slen = len;
    }
    memcpy(dst, src, slen);
}

static void setIpBytes(uint8_t* dst, const char* ipStr)
{
    uint32_t addr = inet_addr(ipStr);
    dst[0] = (uint8_t)((addr >> 24) & 0xFF);
    dst[1] = (uint8_t)((addr >> 16) & 0xFF);
    dst[2] = (uint8_t)((addr >> 8) & 0xFF);
    dst[3] = (uint8_t)(addr & 0xFF);
}

static void handlePoll(const uint8_t* pkt, uint16_t len, uint32_t sourceIp)
{
    (void)pkt;
    (void)len;

    uint8_t reply[ARTPOLLREPLY_SIZE];
    memset(reply, 0, sizeof(reply));

    /* 0-7: ID */
    memcpy(reply, ARTNET_ID, 8);

    /* 8-9: Opcode 0x2100 (PollReply, little-endian) */
    reply[8]  = 0x00;
    reply[9]  = 0x21;

    /* 10-11: Protocol version (high=14, low=0) */
    reply[10] = 14;
    reply[11] = 0;

    /* 12-13: Answers / Addresses */
    reply[12] = 1;
    reply[13] = 0;

    /* 14-15: Bind output/input ports */
    int enabledOutputs = 0;
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (cfg.outputs[i].en) {
            enabledOutputs++;
        }
    }
    reply[14] = (uint8_t)enabledOutputs;
    reply[15] = 0;

    /* 16-17: Node type (0x01 = dynamic) */
    reply[16] = 0x01;
    reply[17] = 0x00;

    /* 18-21: ESTA manufacturer code (placeholder, left zeroed) */

    /* 22-39: Short name */
    fillNameField(reply + 22, "LuxDMX V2", 18);

    /* 40-73: Long name */
    fillNameField(reply + 40, "LuxDMX V2 Art-Net/sACN to DMX Gateway", 34);

    /* 74-137: Node report */
    fillNameField(reply + 74, "", 64);

    /* 138-147: Spare (zeroed) */

    /* 148-159: Node IP, subnet mask, gateway */
    char ip[16];
    char netmask[16];
    char gateway[16];
    if (wifi_get_ip_info(ip, netmask, gateway) == ESP_OK) {
        setIpBytes(reply + 148, ip);
        setIpBytes(reply + 152, netmask);
        setIpBytes(reply + 156, gateway);
    }

    /* 160-167: Spare (zeroed) */

    /* 168-171: Port types (0x80 = DMX) per port */
    memset(reply + 168, 0x80, 4);

    /* 172-175: Port statuses (0x80 if enabled, 0x00 otherwise) */
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        reply[172 + i] = cfg.outputs[i].en ? 0x80 : 0x00;
    }

    /* 176-181: Node MAC (esp_wifi_get_mac) */
    uint8_t mac[6];
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        memcpy(reply + 176, mac, 6);
    }

    /* 182-239: Spare (zeroed) */

    /* Transmit the reply back to the requesting controller */
    if (!g_artnet.ready || g_artnet.sock < 0) {
        LOG_DEBUG(TAG, "ArtPollReply skipped: socket unavailable");
        return;
    }

    struct sockaddr_in dest = {
        .sin_family = AF_INET,
        .sin_port = htons(ARTNET_PORT),
        .sin_addr.s_addr = htonl(sourceIp)
    };

    if (sendto(g_artnet.sock, reply, sizeof(reply), 0,
               (struct sockaddr*)&dest, sizeof(dest)) < 0) {
        LOG_WARN(TAG, "ArtPollReply sendto failed");
    } else {
        LOG_DEBUG(TAG, "ArtPollReply sent to %u", sourceIp);
    }
}

void bridgeDispatch(uint16_t opcode, const uint8_t* pkt, uint16_t len, uint32_t sourceIp)
{
    if (pkt == NULL || len < ARTNET_MIN_PACKET) {
        LOG_WARN(TAG, "dropping short packet (len=%u) from %u", len, sourceIp);
        return;
    }

    if (memcmp(pkt, ARTNET_ID, sizeof(ARTNET_ID) - 1) != 0) {
        LOG_WARN(TAG, "non-ArtNet packet from %u", sourceIp);
        return;
    }

    switch (opcode) {
    case 0x2000:                                  /* ArtPoll */
        handlePoll(pkt, len, sourceIp);
        break;

    case 0x6000:                                  /* ArtAddress */
        LOG_DEBUG(TAG, "ArtAddress from %u", sourceIp);
        break;

    case 0x8300:                                  /* ArtRDM */
        if (cfg.artrdm) {
            LOG_DEBUG(TAG, "ArtRDM from %u, len=%u", sourceIp, len);
        } else {
            LOG_DEBUG(TAG, "ArtRDM from %u dropped: rdm disabled", sourceIp);
        }
        break;

    default:
        LOG_DEBUG(TAG, "dropping unknown opcode 0x%04X from %u", opcode, sourceIp);
        break;
    }
}
