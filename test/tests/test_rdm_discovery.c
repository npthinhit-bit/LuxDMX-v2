#include "rdm_discovery.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint64_t responders[3];
    size_t count;
    size_t muted;
} discovery_fixture_t;

static rdm_disc_result_t probe(uint64_t lower, uint64_t upper, rdm_uid_t *single, void *context)
{
    discovery_fixture_t *fixture = context;
    size_t matches = 0;
    uint64_t match = 0;
    for (size_t i = 0; i < fixture->count; ++i) {
        if (fixture->responders[i] >= lower && fixture->responders[i] <= upper) {
            ++matches;
            match = fixture->responders[i];
        }
    }
    if (matches == 0) return RDM_DISC_EMPTY;
    if (matches > 1) return RDM_DISC_COLLISION;
    *single = rdm_uid_unpack(match);
    return RDM_DISC_SINGLE;
}

static bool mute(rdm_uid_t uid, void *context)
{
    discovery_fixture_t *fixture = context;
    (void)uid;
    ++fixture->muted;
    return true;
}

static void test_binary_search(void)
{
    discovery_fixture_t fixture = {
        .responders = {0x010200000001ULL, 0x010200000100ULL, 0x4C5800001234ULL},
        .count = 3,
        .muted = 0,
    };
    rdm_uid_t found[4] = {0};
    const size_t count = rdmDiscRange(0, RDM_UID_MAX, 3, found, 4, probe, mute, &fixture);
    assert(count == 3);
    assert(fixture.muted == 3);
    assert(rdm_uid_pack(found[0]) == fixture.responders[0] ||
           rdm_uid_pack(found[0]) == fixture.responders[1] ||
           rdm_uid_pack(found[0]) == fixture.responders[2]);
}

static void test_discovery_parser(void)
{
    const uint8_t uid_wire[] = {0x4C, 0x58, 0x00, 0x00, 0x12, 0x34};
    uint8_t response[17] = {0xFE, 0xFE, 0xAA};
    for (size_t i = 0; i < RDM_UID_BYTES; ++i) {
        response[3 + i * 2] = (uint8_t)(0xF0 | (uid_wire[i] >> 4));
        response[4 + i * 2] = (uint8_t)(0xF0 | (uid_wire[i] & 0x0F));
    }
    uint16_t checksum = 0;
    for (size_t i = 0; i < RDM_UID_BYTES; ++i) checksum = (uint16_t)(checksum + uid_wire[i] + 0xFFu);
    response[15] = (uint8_t)(checksum >> 8);
    response[16] = (uint8_t)checksum;

    rdm_uid_t uid = {0};
    assert(rdmParseDiscoveryResponse(response, sizeof(response), &uid) == RDM_DISC_SINGLE);
    assert(rdm_uid_pack(uid) == 0x4C5800001234ULL);
    response[16] ^= 1u;
    assert(rdmParseDiscoveryResponse(response, sizeof(response), &uid) == RDM_DISC_COLLISION);
}

int main(void)
{
    test_binary_search();
    test_discovery_parser();
    puts("rdm_discovery_test: 2 tests passed");
    return 0;
}
