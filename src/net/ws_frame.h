#pragma once
#include <stdint.h>

// WebSocket binary frame layout constants.
// The frame is a flat 2090-byte blob: 16 header + 2048 DMX + 20 output FPS + 10 nav tail.
static constexpr int WS_CHANS_PER_OUT = 512;
static constexpr int WS_CHANS_ALL      = WS_CHANS_PER_OUT * 4;       // 2048
static constexpr int WS_PER_OUT        = 5;
static constexpr int WS_PEROUT_ALL     = WS_PER_OUT * 4;             // 20
static constexpr int WS_NAV_TAIL       = 10;
static constexpr int WS_FRAME_LEN      = 16 + WS_CHANS_ALL + WS_PEROUT_ALL + WS_NAV_TAIL;  // 2090

// Static frame buffer — allocated once, reused every push (avoids heap fragmentation
// under Art-Net flood, per issue #64 investigation).
extern uint8_t wsBuf[];

// Build the binary frame from current state: header stats, all 4 outputs' DMX,
// per-output output/input FPS, TX style, and RDM counters. Caller sends wsBuf
// over the WebSocket after this returns.
void wsBuildFrame();
