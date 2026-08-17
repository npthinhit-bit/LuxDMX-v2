// Unity unit tests for the generic seqlock (include/seqlock.h).
// Verifies clean snapshot, stable write, and multiple write+read cycles.
#include "seqlock.h"
#include <unity.h>

struct Frame
{
    uint8_t data[8];
};

void setUp(void) {}
void tearDown(void) {}

void test_seqlock_clean_snapshot(void)
{
    Frame   src[1] = {};
    SeqLock sl;
    Frame   dst[1] = {};

    memset(src->data, 0xAA, sizeof(src->data));
    TEST_ASSERT_TRUE(sl.snapshot(src, dst, sizeof(src->data)));
    TEST_ASSERT_EQUAL_HEX8(0xAA, dst->data[0]);
}

void test_seqlock_stable_write(void)
{
    Frame   src[1] = {};
    SeqLock sl;
    Frame   dst[1] = {};

    memset(src->data, 0x42, sizeof(src->data));
    TEST_ASSERT_TRUE(sl.snapshot(src, dst, sizeof(src->data)));
    TEST_ASSERT_EQUAL_HEX8(0x42, dst->data[0]);
}

void test_seqlock_write_then_read(void)
{
    Frame   src[1] = {};
    SeqLock sl;
    Frame   dst[1] = {};

    sl.writeBegin();
    memset(src->data, 0x99, sizeof(src->data));
    sl.writeEnd();
    TEST_ASSERT_TRUE(sl.snapshot(src, dst, sizeof(src->data)));
    TEST_ASSERT_EQUAL_HEX8(0x99, dst->data[0]);
}

void test_seqlock_cycles(void)
{
    Frame   src[1] = {};
    SeqLock sl;
    Frame   dst[1] = {};

    for (int cycle = 0; cycle < 100; cycle++)
    {
        sl.writeBegin();
        for (int b = 0; b < 8; b++)
            src->data[b] = (uint8_t)(cycle + b);
        sl.writeEnd();
        TEST_ASSERT_TRUE(sl.snapshot(src, dst, sizeof(src->data)));
        TEST_ASSERT_EQUAL_HEX8((uint8_t)cycle, dst->data[0]);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(cycle + 7), dst->data[7]);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_seqlock_clean_snapshot);
    RUN_TEST(test_seqlock_stable_write);
    RUN_TEST(test_seqlock_write_then_read);
    RUN_TEST(test_seqlock_cycles);
    return UNITY_END();
}
