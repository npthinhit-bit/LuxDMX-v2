#pragma once
#include <stdint.h>
#include <stddef.h>

// Route one received universe frame to every enabled output mapped to it.
// Called by Art-Net and sACN receive (on core 0). Updates sender tracking
// and triggers the merge engine for each affected output.
void routeFrame(int artUniverse, const uint8_t* data, uint16_t length,
                uint32_t senderIp, uint8_t proto, uint8_t priority);
