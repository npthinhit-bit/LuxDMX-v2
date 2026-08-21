/*
 * spec 05 - Sender Tracker
 *
 * Tracks up to 16 active network DMX sources keyed by (source IP, protocol).
 * Maintains a per-sender cache of the most recent frame data, universe,
 * E1.31 priority, frame rate and activity timestamp. Consumed by the frame
 * router / merge engine; produced by the Art-Net and sACN protocol layers via
 * the Frame Router updateSender() call.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- spec 05 section 8 compile-time constants ---- */
#define MAX_SENDERS        16
#define SOURCE_TIMEOUT_MS  2500
#define ACTIVE_WINDOW_MS   5000   /* spec 05 section 8: 5 s display window        */
#define FPS_WINDOW_MS      1000   /* spec 05 section 8: 1 s FPS computation window */
#define DMX_SLOT_COUNT     512    /* spec 05 section 9: 512-byte per-entry data buffer */

/* ---- Protocol discriminators (spec 05 section 13) ---- */
#define PROTO_ARTNET       0
#define PROTO_SACN         1

/* ---- Source-status codes (spec 05 section 7) ---- */
#define SRC_STATUS_NORMAL     0
#define SRC_STATUS_CONFLICT   1
#define SRC_STATUS_MERGING    2

/* Per-sender cache entry (spec 05 section 2 output table).
 *
 * Cross-core discipline (spec 05 section 10): updateSender() writes on core 0;
 * the merge engine reads on core 1 with no mutex. 32-bit aligned scalars are
 * atomic on the ESP32; the only multi-byte write is the 512-byte data buffer
 * memcpy, so dataLen is committed AFTER data is copied -- guaranteeing a reader
 * never observes a length larger than the bytes currently stored.
 *
 * winMs / winCnt are internal FPS-window counters (spec 05 section 4 step 5/8);
 * not consumed by the merge engine but required for rolling frame-rate. */
typedef struct {
    uint32_t ip;                  /* source IPv4 address, host order            */
    uint8_t  proto;               /* PROTO_ARTNET or PROTO_SACN                 */
    uint16_t universe;            /* 15-bit port address: net<<8|sub<<4|uni     */
    uint8_t  priority;            /* E1.31 priority 0-255                       */
    uint8_t  data[DMX_SLOT_COUNT]; /* cached DMX slot data (start code stripped) */
    uint16_t dataLen;             /* valid bytes in data[]; written AFTER data  */
    uint16_t fps;                 /* frames/sec over the last FPS_WINDOW_MS     */
    uint32_t lastMs;              /* last activity timestamp (ms since boot)    */
    uint32_t winMs;               /* FPS window start timestamp (ms)            */
    uint32_t winCnt;              /* frame count within the current FPS window  */
} SenderEntry;

/* Sender table -- static DRAM, zero-initialized (spec 05 section 6). All slot
 * state is implicit: ip==0 means Empty, lastMs within SOURCE_TIMEOUT_MS means
 * Active, otherwise Stale (evicted lazily by the slot-reuse policy). */
extern SenderEntry g_senders[MAX_SENDERS];

/* Zero-initialize the sender table (spec 05 section 6: startup zero-init). */
void senderTrackerInit(void);

/* Cache/update a routed network frame (spec 05 section 4 steps 4-8).
 * Applies the four-tier slot-allocation policy:
 *   (1) match existing sender (same ip + proto)
 *   (2) empty slot
 *   (3) slot whose universe is no longer mapped to any enabled output
 *   (4) oldest sender by lastMs
 * Returns void -- allocation always succeeds (spec 05 section 7). */
void updateSender(uint32_t ip, uint8_t proto, uint16_t universe,
                  uint8_t priority, const uint8_t* data, uint16_t length);

/* Count senders active within the 5 s display window (spec 05 section 2, 8). */
int activeSenderCount(void);

/* Count active sources on a universe within windowMs (spec 05 section 4 step 9). */
int sourcesOnUniverse(uint16_t universe, uint32_t windowMs);

/* Compute the 0/1/2 source-status code across enabled outputs.
 *  0 = normal (no universe has >1 active source)
 *  1 = conflict (>=1 enabled output has >1 active source, merge off)
 *  2 = merging (>=1 merge-enabled output has >1 active source) */
int sourceStatus(void);

#ifdef __cplusplus
}
#endif