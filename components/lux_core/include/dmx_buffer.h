#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "seqlock.h"

/* Spec 03 §5.2 / Spec 45 §11: DMX512 slot buffer (1 start bit + 512 slots). */

#ifdef __cplusplus
extern "C" {
#endif

#define DMX_PACKET_SIZE 513
#define MAX_OUTPUTS 4

/* Per-output live DMX buffer. The SeqLock (odd=writer, even=stable)
   guards concurrent single-writer / single-reader access per spec 45 §11. */
typedef struct {
    uint8_t data[DMX_PACKET_SIZE];
    SeqLock seq;
} DmxBuffer;

/* Global state for all DMX outputs. Staged buffers accumulate ArtNet/sACN
   slot data before an ArtSync triggers a commit into the live buffers. */
typedef struct {
    DmxBuffer buffers[MAX_OUTPUTS];
    uint8_t staged[MAX_OUTPUTS][DMX_PACKET_SIZE];
    bool stagedValid[MAX_OUTPUTS];
    int stagedLen[MAX_OUTPUTS];
    uint8_t sacnStaged[MAX_OUTPUTS][DMX_PACKET_SIZE];
    bool sacnStagedValid[MAX_OUTPUTS];
    int sacnStagedLen[MAX_OUTPUTS];
    uint16_t sacnSyncAddr[MAX_OUTPUTS];
    uint32_t tornSkips;
} DmxBufferState;

extern DmxBufferState g_dmxBufState;

/* Writer: begin a write transaction on output idx's live buffer. */
void dmxBufWriteBegin(int idx);

/* Writer: end a write transaction on output idx's live buffer. */
void dmxBufWriteEnd(int idx);

/* Writer: atomically update a single channel (begin + set + end). */
void dmxBufWriteEndSet(int idx, int channel, uint8_t value);

/* Reader: copy the live buffer for output idx into outFrame (DMX_PACKET_SIZE bytes).
   Returns true on a consistent snapshot, false if torn after 8 retries (spec 03 §4.4). */
bool dmxBufSnapshot(int idx, uint8_t* outFrame);

/* Commit the staged ArtNet buffer for output idx into the live buffer. */
void commitArtSyncStaged(int idx);

/* Commit all staged ArtNet buffers across all outputs. */
void flushArtSyncStaged(void);

void dmx_tx_task_start(void);
void sanitizeOutputs(void);
void outputInitAll(void);

#ifdef __cplusplus
}
#endif

