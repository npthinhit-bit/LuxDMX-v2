#include "rdm_types.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_uid_round_trip(void)
{
    const rdm_uid_t original = {.manufacturer_id = 0x4C58, .device_id = 0x12345678};
    const uint64_t packed = rdm_uid_pack(original);
    const rdm_uid_t unpacked = rdm_uid_unpack(packed);
    assert(packed == 0x4C5812345678ULL);
    assert(rdm_uid_equal(original, unpacked));
}

static void test_wire_order(void)
{
    const rdm_uid_t uid = {.manufacturer_id = 0x0102, .device_id = 0xA0B0C0D0};
    uint8_t wire[RDM_UID_BYTES] = {0};
    rdm_uid_to_wire(uid, wire);
    assert(memcmp(wire, (uint8_t[]){0x01, 0x02, 0xA0, 0xB0, 0xC0, 0xD0}, RDM_UID_BYTES) == 0);
    assert(rdm_uid_equal(uid, rdm_uid_from_wire(wire)));
}

static void test_contract_constants(void)
{
    assert(RDM_SC == 0xCC);
    assert(RDM_SUB_SC == 0x01);
    assert(RDM_UID_BROADCAST_ALL == RDM_UID_MAX);
    assert(RDM_PID_DISC_UNIQUE_BRANCH == 0x0001);
    assert(RDM_PID_DISC_MUTE == 0x0002);
    assert(RDM_PID_DISC_UN_MUTE == 0x0003);
    assert(RDM_CC_GET_COMMAND == 0x20);
    assert(RDM_RESPONSE_ACK_TIMER == 2);
}

int main(void)
{
    test_uid_round_trip();
    test_wire_order();
    test_contract_constants();
    puts("rdm_types_test: 3 tests passed");
    return 0;
}
