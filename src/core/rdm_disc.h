#pragma once
// RDM discovery (E1.20 DISC_UNIQUE_BRANCH). Split out from rdm_engine.cpp because it
// needs its own transport primitives (the iterative binary search with a branch budget)
// and keeping it separate keeps rdm_engine.cpp under the file-size guideline.
// All discovery runs on the DMX task thread (the sole owner of the RMT channel).
#include "rdm_types.h"

uint64_t uidPack(const rdm_uid_t& u);
rdm_uid_t uidUnpack(uint64_t v);

// Send DISC_UNIQUE_BRANCH(lower..upper). Returns 0 = empty, 1 = one found, 2 = collision.
int rdmDiscBranch(uint64_t lower, uint64_t upper, rdm_uid_t* found);

// Mute a single device so it drops out of further branch sweeps. Returns true on ACK.
bool rdmMute(const rdm_uid_t& uid);

// Un-mute everyone (broadcast) before a fresh discovery sweep.
void rdmUnMuteAll();

// Iterative binary search over [lo0..hi0], muting + recording each device. Results in out[].
void rdmDiscRange(uint64_t lo0, uint64_t hi0, rdm_uid_t* out, int max, int* count);

// Full discovery sweep. Returns the number of devices found (<= max).
int rdmRmtDiscover(rdm_uid_t* out, int max);
