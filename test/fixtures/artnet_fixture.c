#include "artnet_fixture.h"

#include <string.h>

static void put_u16_le(uint8_t* dst, uint16_t value) {
    dst[0] = (uint8_t)(value & 0xffu);
    dst[1] = (uint8_t)(value >> 8);
}

static void put_u16_be(uint8_t* dst, uint16_t value) {
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)(value & 0xffu);
}

bool lux_artnet_fixture_init(lux_artnet_fixture_t* fixture,
                             uint16_t universe,
                             uint8_t sequence,
                             uint8_t priority,
                             const uint8_t* slots,
                             size_t slot_count) {
    if (!fixture || (slot_count > 0 && !slots) || slot_count > LUX_ARTDMX_MAX_SLOTS) {
        return false;
    }

    memset(fixture, 0, sizeof(*fixture));
    fixture->contract = (lux_test_fixture_contract_t){
        .name = "artnet_artdmx",
        .timeout_ms = 5000,
        .artifact_dir = "artifacts/artnet_artdmx",
        .requires_hil = false,
    };
    if (!lux_test_fixture_contract_is_valid(&fixture->contract)) {
        return false;
    }

    memcpy(fixture->packet, ARTNET_ID, 8);
    put_u16_le(&fixture->packet[8], 0x5000); /* ArtDMX */
    put_u16_be(&fixture->packet[10], 14);    /* protocol version 1.4 */
    fixture->packet[12] = sequence;
    fixture->packet[13] = 0;                /* physical port */
    put_u16_le(&fixture->packet[14], universe);
    put_u16_be(&fixture->packet[16], (uint16_t)slot_count);
    fixture->packet[59] = priority;
    if (slot_count > 0) {
        memcpy(&fixture->packet[LUX_ARTDMX_DATA_OFFSET], slots, slot_count);
    }
    fixture->packet_length = LUX_ARTDMX_DATA_OFFSET + slot_count;
    return fixture->packet_length <= ARTNET_MAX_PACKET;
}
