// Unity unit tests for the DMX merge engine.
// Verifies HTP merge, OFF last-frame-wins, signal-loss/zero, LTP-takeover,
// priority merge, and failsafe timeout behavior.
#include "config_enums.h"
#include "config_schema.h"
#include "dmx_buffer.h"
#include "merge_engine.h"
#include "sender_tracker.h"
#include "stats.h"
#include <unity.h>

static void resetOutput(int i)
{
    cfg.outputs[i].enabled         = true;
    cfg.outputs[i].universe        = 1;
    cfg.outputs[i].net             = 0;
    cfg.outputs[i].subnet          = 0;
    cfg.outputs[i].mergeMode       = MERGE_OFF;
    cfg.outputs[i].lossMode        = LOSS_HOLD;
    cfg.outputs[i].failsafeTimeout = 0;
}

static void clearSenders()
{
    memset(senderTracker().senders, 0, MAX_SENDERS * sizeof(Sender));
}

void setUp(void)
{
    clearSenders();
    for (int i = 0; i < MAX_OUTPUTS; i++)
    {
        memset(&dmxBufferState().buffers[i].data[1], 0, 512);
        stats().outSrcLost[i]  = true;
        stats().rxLossCount[i] = 0;
    }
    for (int i = 0; i < MAX_SENDERS; i++)
        senderTracker().senders[i].lastMs = 0;
    resetOutput(0);
}
void tearDown(void) {}

void test_merge_htp(void)
{
    uint8_t f1[512], f2[512];
    memset(f1, 100, sizeof(f1));
    memset(f2, 200, sizeof(f2));
    cfg.outputs[0].mergeMode = MERGE_HTP;
    updateSender(0xC0A80101, 0, 1, 100, f1, 512);
    updateSender(0xC0A80102, 0, 1, 100, f2, 512);
    mergeOutput(0);
    uint8_t snap[DMX_PACKET_SIZE];
    TEST_ASSERT_TRUE(dmxBufSnapshot(0, snap));
    TEST_ASSERT_EQUAL_HEX8(200, snap[1]);
}

void test_merge_off_last_wins(void)
{
    uint8_t f1[512], f2[512];
    memset(f1, 50, sizeof(f1));
    memset(f2, 200, sizeof(f2));
    cfg.outputs[0].mergeMode = MERGE_OFF;
    updateSender(0xC0A80101, 0, 1, 100, f1, 512);
    updateSender(0xC0A80102, 0, 1, 100, f2, 512);
    mergeOutput(0);
    uint8_t snap[DMX_PACKET_SIZE];
    TEST_ASSERT_TRUE(dmxBufSnapshot(0, snap));
    TEST_ASSERT_EQUAL_HEX8(200, snap[1]);
}

void test_merge_loss_zero(void)
{
    cfg.outputs[0].lossMode  = LOSS_ZERO;
    cfg.outputs[0].mergeMode = MERGE_HTP;
    mergeOutput(0);
    uint8_t snap[DMX_PACKET_SIZE];
    TEST_ASSERT_TRUE(dmxBufSnapshot(0, snap));
    TEST_ASSERT_EQUAL_HEX8(0, snap[1]);
}

void test_merge_diff_universe_ignored(void)
{
    uint8_t f[512];
    memset(f, 123, sizeof(f));
    cfg.outputs[0].mergeMode = MERGE_OFF;
    updateSender(0xC0A80101, 0, 2, 100, f, 512);
    mergeOutput(0);
    uint8_t snap[DMX_PACKET_SIZE];
    TEST_ASSERT_TRUE(dmxBufSnapshot(0, snap));
    TEST_ASSERT_EQUAL_HEX8(0, snap[1]);
}

void test_merge_ltp_takeover_priority(void)
{
    uint8_t f1[512], f2[512];
    memset(f1, 100, sizeof(f1));
    memset(f2, 200, sizeof(f2));
    cfg.outputs[0].mergeMode = MERGE_LTP_TAKEOVER;
    updateSender(0xC0A80103, 0, 1, 100, f1, 512);
    senderTracker().senders[0].lastMs = 999999;
    updateSender(0xC0A80104, 0, 1, 200, f2, 512);
    mergeOutput(0);
    uint8_t snap[DMX_PACKET_SIZE];
    TEST_ASSERT_TRUE(dmxBufSnapshot(0, snap));
    TEST_ASSERT_EQUAL_HEX8(200, snap[1]);
}

void test_merge_priority_only_top(void)
{
    uint8_t f1[512], f2[512], f3[512];
    memset(f1, 100, sizeof(f1));
    memset(f2, 200, sizeof(f2));
    memset(f3, 150, sizeof(f3));
    cfg.outputs[0].mergeMode = MERGE_PRIORITY;
    updateSender(0xC0A80101, 0, 1, 100, f1, 512);
    updateSender(0xC0A80102, 0, 1, 200, f2, 512);
    updateSender(0xC0A80103, 0, 1, 150, f3, 512);
    mergeOutput(0);
    uint8_t snap[DMX_PACKET_SIZE];
    TEST_ASSERT_TRUE(dmxBufSnapshot(0, snap));
    TEST_ASSERT_EQUAL_HEX8(200, snap[1]);
}

void test_merge_failsafe_within_window(void)
{
    uint8_t f[512];
    memset(f, 255, sizeof(f));
    cfg.outputs[0].failsafeTimeout = 0;
    cfg.outputs[0].lossMode        = LOSS_ZERO;
    updateSender(0xC0A80101, 0, 1, 100, f, 512);
    mergeOutput(0);
    uint8_t snap[DMX_PACKET_SIZE];
    TEST_ASSERT_TRUE(dmxBufSnapshot(0, snap));
    TEST_ASSERT_EQUAL_HEX8(255, snap[1]);
}

void test_merge_ltp_newest_wins(void)
{
    uint8_t f1[512], f2[512];
    memset(f1, 100, sizeof(f1));
    memset(f2, 200, sizeof(f2));
    cfg.outputs[0].mergeMode = MERGE_LTP;
    updateSender(0xC0A80101, 0, 1, 100, f1, 512);
    updateSender(0xC0A80102, 0, 1, 100, f2, 512);
    mergeOutput(0);
    uint8_t snap[DMX_PACKET_SIZE];
    TEST_ASSERT_TRUE(dmxBufSnapshot(0, snap));
    TEST_ASSERT_EQUAL_HEX8(200, snap[1]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_merge_htp);
    RUN_TEST(test_merge_off_last_wins);
    RUN_TEST(test_merge_loss_zero);
    RUN_TEST(test_merge_diff_universe_ignored);
    RUN_TEST(test_merge_ltp_takeover_priority);
    RUN_TEST(test_merge_priority_only_top);
    RUN_TEST(test_merge_failsafe_within_window);
    RUN_TEST(test_merge_ltp_newest_wins);
    return UNITY_END();
}
