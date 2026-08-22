#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "artnet.h"
#include "test_fixture_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUX_ARTDMX_DATA_OFFSET 60
#define LUX_ARTDMX_MAX_SLOTS 512

typedef struct {
    lux_test_fixture_contract_t contract;
    uint8_t packet[ARTNET_MAX_PACKET];
    size_t packet_length;
} lux_artnet_fixture_t;

bool lux_artnet_fixture_init(lux_artnet_fixture_t* fixture,
                             uint16_t universe,
                             uint8_t sequence,
                             uint8_t priority,
                             const uint8_t* slots,
                             size_t slot_count);

#ifdef __cplusplus
}
#endif
