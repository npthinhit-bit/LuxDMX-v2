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
#include <string.h>

static const char* TAG = "artBridge";

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
        LOG_DEBUG(TAG, "ArtPoll from %u", sourceIp);
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

