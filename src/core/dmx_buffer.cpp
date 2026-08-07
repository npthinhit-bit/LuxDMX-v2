#include "dmx_buffer.h"

DmxBuffer dmxBuffers[DMX_OUTPUT_COUNT] = {};
uint32_t dmxTornSkips = 0;

bool dmxBufSnapshot(int i, uint8_t* out) {
    for (int tries = 0; tries < 8; tries++) {
        const uint32_t s1 = dmxBuffers[i].seq;
        if (s1 & 1u) continue;
        __sync_synchronize();
        memcpy(out, dmxBuffers[i].data, DMX_PACKET_SIZE);
        __sync_synchronize();
        if (dmxBuffers[i].seq == s1) return true;
    }
    dmxTornSkips++;
    return false;
}
