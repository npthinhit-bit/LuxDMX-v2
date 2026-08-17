#pragma once
#include "config_schema.h"
#include <stddef.h>
#include <stdint.h>

// Compute the full 15-bit Art-Net port address for an output:
// (net << 8) | (subnet << 4) | universe.
inline uint16_t portAddress(const DmxOutput& o)
{
    return (uint16_t)((o.net << 8) | (o.subnet << 4) | o.universe);
}

// Route one received universe frame to every enabled output mapped to it.
// Called by Art-Net and sACN receive (on core 0). Updates sender tracking
// and triggers the merge engine for each affected output.
void routeFrame(int artUniverse, const uint8_t* data, uint16_t length, uint32_t senderIp, uint8_t proto,
                uint8_t priority);

// Same as routeFrame but for ArtNzs (non-zero start code). The start code
// is written into data[0] of the staged buffer.
void routeFrameNzs(int artUniverse, uint8_t* data, uint16_t length, uint8_t startCode, uint32_t senderIp,
                   uint8_t priority);
