#include "dmx_buffer.h"

DmxBufferState g_dmxBufState;

void dmxBufWriteBegin(int idx) {
    seqlock_write_begin(&g_dmxBufState.buffers[idx].seq);
}

void dmxBufWriteEnd(int idx) {
    seqlock_write_end(&g_dmxBufState.buffers[idx].seq);
}

void dmxBufWriteEndSet(int idx, int channel, uint8_t value) {
    if (channel < 1 || channel >= DMX_PACKET_SIZE) return;
    seqlock_write_begin(&g_dmxBufState.buffers[idx].seq);
    g_dmxBufState.buffers[idx].data[channel] = value;
    seqlock_write_end(&g_dmxBufState.buffers[idx].seq);
}

bool dmxBufSnapshot(int idx, uint8_t* outFrame) {
    bool ok = seqlock_snapshot(&g_dmxBufState.buffers[idx].seq,
                               outFrame,
                               g_dmxBufState.buffers[idx].data,
                               DMX_PACKET_SIZE);
    if (!ok) {
        g_dmxBufState.tornSkips++;
    }
    return ok;
}

void commitArtSyncStaged(int idx) {
    if (idx < 0 || idx >= MAX_OUTPUTS) return;
    if (!g_dmxBufState.stagedValid[idx]) return;
    seqlock_write_begin(&g_dmxBufState.buffers[idx].seq);
    memcpy(g_dmxBufState.buffers[idx].data,
           g_dmxBufState.staged[idx],
           DMX_PACKET_SIZE);
    seqlock_write_end(&g_dmxBufState.buffers[idx].seq);
    g_dmxBufState.stagedValid[idx] = false;
}

void commitSacnStaged(int outputIdx) {
    if (outputIdx < 0 || outputIdx >= MAX_OUTPUTS) return;
    if (!g_dmxBufState.sacnStagedValid[outputIdx]) return;
    uint16_t len = g_dmxBufState.sacnStagedLen[outputIdx];
    dmxBufWriteBegin(outputIdx);
    g_dmxBufState.buffers[outputIdx].data[0] = 0; /* start code */
    memcpy(g_dmxBufState.buffers[outputIdx].data + 1,
           g_dmxBufState.sacnStaged[outputIdx], len);
    if (len < DMX_PACKET_SIZE - 1) {
        memset(g_dmxBufState.buffers[outputIdx].data + 1 + len,
               0, DMX_PACKET_SIZE - 1 - len);
    }
    dmxBufWriteEnd(outputIdx);
    g_dmxBufState.sacnStagedValid[outputIdx] = false;
}

void flushArtSyncStaged(void) {
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (g_dmxBufState.stagedValid[i]) {
            commitArtSyncStaged(i);
            g_dmxBufState.sacnSyncDeadlineMs[i] = 0;
        }
    }
}
