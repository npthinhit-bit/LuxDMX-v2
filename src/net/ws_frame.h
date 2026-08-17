#pragma once
#include "config_schema.h"
#include <stdint.h>

// WebSocket binary frame layout:
//   [16-byte header][512*MAX_OUTPUTS DMX][5*MAX_OUTPUTS per-output stats][1 changed-bitmap][10-byte RDM tail]
static constexpr int WS_HEADER_LEN    = 16;
static constexpr int WS_CHANS_PER_OUT = 512;
static constexpr int WS_CHANS_ALL     = WS_CHANS_PER_OUT * MAX_OUTPUTS;  // 2048
static constexpr int WS_PER_OUT       = 5;
static constexpr int WS_PEROUT_ALL    = WS_PER_OUT * MAX_OUTPUTS;  // 20
static constexpr int WS_NAV_TAIL      = 10;
static constexpr int WS_CHANGED_OFF   = WS_HEADER_LEN + WS_CHANS_ALL + WS_PEROUT_ALL;  // 2084
static constexpr int WS_FRAME_LEN     = WS_CHANGED_OFF + 1 + WS_NAV_TAIL;              // 2095
static constexpr int WS_MAX_CLIENTS   = 12;

// Static frame buffer — allocated once, reused every push (avoids heap fragmentation
// under Art-Net flood, per issue #64 investigation).
extern uint8_t wsBuf[];

// Changed-universe bitmap: bit i set if universe i's DMX changed since the last
// wsBuildFrame() call. Computed in wsBuildFrame(), read by wsPush().
extern uint8_t wsChangedBitmap;

// Frame sequence counter, incremented once per wsBuildFrame call.
extern uint32_t wsFrameSeq;

// Per-client subscription bitmask: bit i = universe i subscribed.
// Default all universes subscribed on connect. Indexed by client->id() % WS_MAX_CLIENTS.
extern uint16_t wsClientSub[WS_MAX_CLIENTS];

// Build the binary frame from current state: header stats, all outputs' DMX,
// changed-universe bitmap, per-output output/input FPS, TX style, and RDM counters.
// Caller sends wsBuf over the WebSocket after this returns.
void wsBuildFrame();