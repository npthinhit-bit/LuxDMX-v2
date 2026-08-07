#pragma once
#include <stdint.h>
#include <string.h>
#include "rdm_types.h"   // DMX_PACKET_SIZE
#include "config_schema.h"

#define DMX_OUTPUT_COUNT MAX_OUTPUTS

// DRAM-resident per-output DMX frame + seqlock.
// Written by the receive/merge task (core 0), read by the DMX transmit task (core 1).
struct DmxBuffer {
    uint8_t            data[DMX_PACKET_SIZE];
    volatile uint32_t  seq;
};

extern DmxBuffer dmxBuffers[DMX_OUTPUT_COUNT];
extern uint32_t  dmxTornSkips;

// Writer side: wrap a memcpy into dmxBuffers[i].data[1..512].
inline void dmxBufWriteBegin(int i) { dmxBuffers[i].seq++; __sync_synchronize(); }
inline void dmxBufWriteEnd(int i)   { __sync_synchronize(); dmxBuffers[i].seq++; }

// Set a single channel (1-based) and commit. Used by the WS manual-control path.
inline void dmxBufWriteEndSet(int i, int ch, uint8_t val) {
    if (ch >= 1 && ch < DMX_PACKET_SIZE) dmxBuffers[i].data[ch] = val;
    dmxBufWriteEnd(i);
}

// Reader side: take a clean snapshot of one output's frame.
// Returns true if a consistent copy was obtained (caller may transmit it).
// false means the writer raced us 8 times straight -- hold the previous frame.
bool dmxBufSnapshot(int i, uint8_t* out);
