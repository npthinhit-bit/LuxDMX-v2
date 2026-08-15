#pragma once
#include <stdint.h>
#include "config_enums.h"

// Merge one output's frame from its live sources. Called after updateSender()
// caches a new frame, before the DMX task clocks it out. Honours E1.31 priority
// first (only top-priority sources contribute), then the per-output merge mode:
//   OFF — last seen source wins the whole frame (legacy)
//   HTP — per-channel max across top-priority sources
//   LTP — most recently seen top-priority source wins
// On source loss, the per-output lossMode applies (HOLD/Zero handled here;
// STOP is enforced by the transmit task via outSrcLost[]).
void mergeOutput(int outIdx);

void mergeOutputTimed();

