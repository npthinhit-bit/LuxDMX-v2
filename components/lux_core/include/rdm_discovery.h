#pragma once

#include "rdm_types.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RDM_DISC_EMPTY = 0,
    RDM_DISC_SINGLE = 1,
    RDM_DISC_COLLISION = 2,
} rdm_disc_result_t;

typedef rdm_disc_result_t (*rdm_branch_probe_fn)(uint64_t lower, uint64_t upper,
                                                 rdm_uid_t *single_uid, void *context);
typedef bool (*rdm_mute_fn)(rdm_uid_t uid, void *context);

/* Run the bounded E1.20 binary search. Returns number of unique UIDs found. */
size_t rdmDiscRange(uint64_t lower, uint64_t upper, size_t max_devices,
                    rdm_uid_t *out, size_t out_capacity,
                    rdm_branch_probe_fn probe, rdm_mute_fn mute, void *context);

/* Parse the fill/separator encoded response from a DISC_UNIQUE_BRANCH probe. */
rdm_disc_result_t rdmParseDiscoveryResponse(const uint8_t *response, size_t length,
                                            rdm_uid_t *uid);

#ifdef __cplusplus
}
#endif
