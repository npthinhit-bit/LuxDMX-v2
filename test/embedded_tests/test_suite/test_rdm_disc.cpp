// Unity tests for rdm_disc.cpp UID pack/unpack (uidPack, uidUnpack).
// Pure-logic tests — no hardware interaction needed.
#include "rdm_disc.h"
#include "rdm_types.h"
#include <unity.h>

void test_uid_pack_roundtrip(void)
{
    rdm_uid_t u = {0x4C58, 0x11223344};
    uint64_t  v = uidPack(u);
    rdm_uid_t r = uidUnpack(v);
    TEST_ASSERT_EQUAL_UINT16(u.man_id, r.man_id);
    TEST_ASSERT_EQUAL_UINT32(u.dev_id, r.dev_id);
}

void test_uid_pack_order(void)
{
    rdm_uid_t u = {0x0102, 0x03040506};
    uint64_t  v = uidPack(u);
    uint64_t  expected = ((uint64_t)0x0102 << 32) | 0x03040506;
    TEST_ASSERT_EQUAL_UINT64(expected, v);
}

void test_uid_pack_broadcast(void)
{
    uint64_t  v = uidPack(RDM_UID_BROADCAST_ALL);
    rdm_uid_t r = uidUnpack(v);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, r.man_id);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFF, r.dev_id);
}

void test_uid_pack_max(void)
{
    uint64_t  v = uidPack(RDM_UID_MAX);
    rdm_uid_t r = uidUnpack(v);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, r.man_id);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFE, r.dev_id);
}
