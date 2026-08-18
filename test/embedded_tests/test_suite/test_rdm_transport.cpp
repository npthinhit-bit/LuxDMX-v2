// Unity tests for rdm_engine.cpp transport framing (rdmBuild, putUid).
// Verifies packet layout, checksum, transaction-number increment, and UID packing.
#include "rdm_engine.h"
#include "rdm_types.h"
#include <unity.h>

// Save/restore g_rdm.tn so tests are deterministic and independent.
static uint8_t savedTn;

void test_rdm_build_layout(void)
{
    uint8_t buf[64];
    memset(buf, 0xEE, sizeof(buf));
    rdm_uid_t dest = {0x1234, 0xDEADBEEF};
    uint8_t   pd[2] = {0xAB, 0xCD};
    int       len = rdmBuild(buf, dest, RDM_CC_GET_COMMAND, 0x0060, pd, 2);

    // Length = RDM_HDR_LEN(24) + pdl(2) = 26, plus 2-byte checksum = 28 total
    TEST_ASSERT_EQUAL_INT(28, len);
    TEST_ASSERT_EQUAL_HEX8(RDM_SC, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(RDM_SC_SUB, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(RDM_HDR_LEN + 2, buf[2]);

    // dest UID at [3..8] — big-endian: man_id high, man_id low, dev_id MSB..LSB
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x34, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0xDE, buf[5]);
    TEST_ASSERT_EQUAL_HEX8(0xAD, buf[6]);
    TEST_ASSERT_EQUAL_HEX8(0xBE, buf[7]);
    TEST_ASSERT_EQUAL_HEX8(0xEF, buf[8]);

    // src UID at [9..14] — g_rdm.ctrl default is {0x4C58, 0}
    TEST_ASSERT_EQUAL_HEX8(0x4C, buf[9]);
    TEST_ASSERT_EQUAL_HEX8(0x58, buf[10]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[11]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[12]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[13]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[14]);

    // transaction number at [15]
    TEST_ASSERT_EQUAL_HEX8(0, buf[15]);

    // sub-device at [16..17] = 0x01, 0x00
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[16]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[17]);

    // [18..19] = 0x00, 0x00
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[18]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[19]);

    // CC at [20]
    TEST_ASSERT_EQUAL_HEX8(RDM_CC_GET_COMMAND, buf[20]);

    // PID at [21..22] — big-endian
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[21]);
    TEST_ASSERT_EQUAL_HEX8(0x60, buf[22]);

    // PDL at [23]
    TEST_ASSERT_EQUAL_HEX8(2, buf[23]);

    // Parameter data at [24..25]
    TEST_ASSERT_EQUAL_HEX8(0xAB, buf[24]);
    TEST_ASSERT_EQUAL_HEX8(0xCD, buf[25]);
}

void test_rdm_build_checksum(void)
{
    uint8_t buf[64];
    rdm_uid_t dest = {0x1234, 0xBEEFCAFE};
    int       len = rdmBuild(buf, dest, RDM_CC_GET_COMMAND, 0x0090, nullptr, 0);

    // Compute additive 8-bit checksum over all bytes except the checksum itself
    uint16_t ck = 0;
    for (int i = 0; i < len - 2; i++)
        ck += buf[i];

    uint16_t got = (buf[len - 2] << 8) | buf[len - 1];
    TEST_ASSERT_EQUAL_HEX16(ck & 0xFFFF, got);
}

void test_rdm_build_transaction_increments(void)
{
    uint8_t buf[64];
    rdm_uid_t dest = {0x4321, 0x11223344};

    savedTn = g_rdm.tn;
    g_rdm.tn = 254;

    int len1 = rdmBuild(buf, dest, RDM_CC_GET_COMMAND, 0x0060, nullptr, 0);
    TEST_ASSERT_EQUAL_HEX8(254, buf[15]);
    TEST_ASSERT_EQUAL_UINT8(255, g_rdm.tn);

    int len2 = rdmBuild(buf, dest, RDM_CC_GET_COMMAND, 0x0060, nullptr, 0);
    TEST_ASSERT_EQUAL_HEX8(255, buf[15]);
    TEST_ASSERT_EQUAL_UINT8(0, g_rdm.tn);  // wrapped

    int len3 = rdmBuild(buf, dest, RDM_CC_GET_COMMAND, 0x0060, nullptr, 0);
    TEST_ASSERT_EQUAL_HEX8(0, buf[15]);

    g_rdm.tn = savedTn;
    (void)len1;
    (void)len2;
    (void)len3;
}

void test_rdm_build_no_param_data(void)
{
    uint8_t buf[64];
    rdm_uid_t dest = {0xABCD, 0x00112233};
    int       len = rdmBuild(buf, dest, RDM_CC_GET_COMMAND, 0x00F0, nullptr, 0);

    // No parameter data: length = RDM_HDR_LEN(24) + 0 = 24, total = 26
    TEST_ASSERT_EQUAL_INT(26, len);
    TEST_ASSERT_EQUAL_HEX8(RDM_HDR_LEN, buf[2]);  // length field = 24
    TEST_ASSERT_EQUAL_HEX8(0, buf[23]);           // PDL = 0
}

void test_put_uid_big_endian(void)
{
    rdm_uid_t u = {0x4C58, 0xCAFEBABE};
    uint8_t   p[6];
    putUid(p, u);

    TEST_ASSERT_EQUAL_HEX8(0x4C, p[0]);  // man_id >> 8
    TEST_ASSERT_EQUAL_HEX8(0x58, p[1]);  // man_id & 0xff
    TEST_ASSERT_EQUAL_HEX8(0xCA, p[2]);  // dev_id >> 24
    TEST_ASSERT_EQUAL_HEX8(0xFE, p[3]);  // dev_id >> 16
    TEST_ASSERT_EQUAL_HEX8(0xBA, p[4]);  // dev_id >> 8
    TEST_ASSERT_EQUAL_HEX8(0xBE, p[5]);  // dev_id & 0xff
}
