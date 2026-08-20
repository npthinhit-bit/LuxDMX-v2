/*
 * spec 04 - Merge Engine
 *
 * Blends contributing network senders into the seqlock-protected live DMX
 * buffer per the configured merge / loss mode. Runs core 0 (per-packet
 * routing) and core 1 (1 ms tick); never touches hardware directly.
 */
#pragma once

#include "config_engine.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 15-bit ArtNet port address of an output:
 * (net << 8) | (sub << 4) | uni  (matches SenderEntry.universe, spec 04/05). */
int portAddress(const DmxOutput* out);

/* Merge all live contributors for outIdx into buffers[outIdx].data[1..512]
 * under the seqlock, per cfg.outputs[outIdx].mergemode / .losemode. nowMs is
 * the caller-supplied monotonic ms timestamp. Returns void; silent on error
 * (spec 04). */
void mergeOutput(int outIdx, uint32_t nowMs);

#ifdef __cplusplus
}
#endif