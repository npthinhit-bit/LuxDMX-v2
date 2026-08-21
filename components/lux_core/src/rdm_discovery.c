#include "rdm_discovery.h"

#include <string.h>

#define RDM_DISC_STACK_CAPACITY 64u
#define RDM_DISC_ITERATION_EXTRA 128u

static uint64_t midpoint(uint64_t lower, uint64_t upper)
{
    return lower + ((upper - lower) / 2u);
}

static bool contains_uid(const rdm_uid_t *uids, size_t count, rdm_uid_t uid)
{
    for (size_t i = 0; i < count; ++i) {
        if (rdm_uid_equal(uids[i], uid)) {
            return true;
        }
    }
    return false;
}

size_t rdmDiscRange(uint64_t lower, uint64_t upper, size_t max_devices,
                    rdm_uid_t *out, size_t out_capacity,
                    rdm_branch_probe_fn probe, rdm_mute_fn mute, void *context)
{
    if (out == NULL || out_capacity == 0 || max_devices == 0 || probe == NULL || lower > upper) {
        return 0;
    }
    if (max_devices > out_capacity) {
        max_devices = out_capacity;
    }

    uint64_t lowers[RDM_DISC_STACK_CAPACITY];
    uint64_t uppers[RDM_DISC_STACK_CAPACITY];
    size_t stack_size = 1;
    size_t found = 0;
    size_t iterations = 0;
    const size_t budget = (8u * max_devices) + RDM_DISC_ITERATION_EXTRA;
    lowers[0] = lower;
    uppers[0] = upper;

    while (stack_size != 0 && found < max_devices && iterations++ < budget) {
        --stack_size;
        const uint64_t branch_lower = lowers[stack_size];
        const uint64_t branch_upper = uppers[stack_size];
        rdm_uid_t single = {0};
        const rdm_disc_result_t result = probe(branch_lower, branch_upper, &single, context);

        if (result == RDM_DISC_EMPTY || branch_lower == branch_upper && result != RDM_DISC_SINGLE) {
            continue;
        }
        if (result == RDM_DISC_SINGLE) {
            const uint64_t packed = rdm_uid_pack(single);
            if (packed < branch_lower || packed > branch_upper || contains_uid(out, found, single)) {
                continue;
            }
            if (mute == NULL || mute(single, context)) {
                out[found++] = single;
            }
            continue;
        }
        if (result == RDM_DISC_COLLISION && branch_lower < branch_upper &&
            stack_size + 2u <= RDM_DISC_STACK_CAPACITY) {
            const uint64_t split = midpoint(branch_lower, branch_upper);
            lowers[stack_size] = branch_lower;
            uppers[stack_size] = split;
            ++stack_size;
            lowers[stack_size] = split + 1u;
            uppers[stack_size] = branch_upper;
            ++stack_size;
        }
    }
    return found;
}

rdm_disc_result_t rdmParseDiscoveryResponse(const uint8_t *response, size_t length,
                                            rdm_uid_t *uid)
{
    if (response == NULL || uid == NULL || length < 4u) {
        return RDM_DISC_EMPTY;
    }

    size_t separator = 0;
    while (separator < length && response[separator] != 0xAAu) {
        ++separator;
    }
    if (separator < 2u || separator + 14u >= length) {
        return length == 0 ? RDM_DISC_EMPTY : RDM_DISC_COLLISION;
    }

    /* Six UID bytes are sent as high/low nibble pairs following the separator. */
    uint8_t wire[RDM_UID_BYTES] = {0};
    for (size_t i = 0; i < RDM_UID_BYTES; ++i) {
        const uint8_t high = response[separator + 1u + i * 2u];
        const uint8_t low = response[separator + 2u + i * 2u];
        if ((high & 0xF0u) != 0xF0u || (low & 0xF0u) != 0xF0u) {
            return RDM_DISC_COLLISION;
        }
        wire[i] = (uint8_t)(((high & 0x0Fu) << 4) | (low & 0x0Fu));
    }

    const size_t checksum_offset = separator + 13u;
    if (checksum_offset + 1u >= length) {
        return RDM_DISC_COLLISION;
    }
    uint16_t sum = 0;
    for (size_t i = 0; i < RDM_UID_BYTES; ++i) {
        sum = (uint16_t)(sum + wire[i] + 0xFFu);
    }
    const uint16_t expected = (uint16_t)(((uint16_t)response[checksum_offset] << 8) |
                                         response[checksum_offset + 1u]);
    if (sum != expected) {
        return RDM_DISC_COLLISION;
    }
    *uid = rdm_uid_from_wire(wire);
    return RDM_DISC_SINGLE;
}
