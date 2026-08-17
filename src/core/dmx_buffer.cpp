#include "dmx_buffer.h"
#include <string.h>

static DmxBufferState g_dmxBufState;
DmxBufferState&       dmxBufferState()
{
    return g_dmxBufState;
}

bool dmxBufSnapshot(int i, uint8_t* out)
{
    for (int tries = 0; tries < 8; tries++)
    {
        const uint32_t s1 = dmxBufferState().buffers[i].seq;
        if (s1 & 1u)
            continue;
        __sync_synchronize();
        memcpy(out, dmxBufferState().buffers[i].data, DMX_PACKET_SIZE);
        __sync_synchronize();
        if (dmxBufferState().buffers[i].seq == s1)
            return true;
    }
    dmxBufferState().tornSkips++;
    return false;
}
