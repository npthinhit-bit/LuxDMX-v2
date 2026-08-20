#include "shim.h"
#include "merge_engine.h"
#include "sender_tracker.h"
#include "dmx_buffer.h"
#include "config_engine.h"
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_##name(void) { \
        tests_run++; \
        printf("  [RUN ] %s\n", "test_" #name); \
        test_##name(); \
        tests_passed++; \
        printf("  [PASS] %s\n", "test_" #name); \
    } \
    static void test_##name(void)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        tests_passed--; \
        return; \
    } \
} while(0)

static int find_sender_by_ip(uint32_t ip) {
    for (int i = 0; i < MAX_SENDERS; i++) {
        if (g_senders[i].ip == ip) return i;
    }
    return -1;
}

#define TEST_UNIVERSE 1

TEST(merge_htp_selects_highest_per_channel) {
    memset(&cfg, 0, sizeof(cfg));
    memset(&g_dmxBufState, 0, sizeof(g_dmxBufState));

    cfg.outputs[0].en = 1;
    cfg.outputs[0].uni = 1;
    cfg.outputs[0].mergemode = 1;  /* MERGE_HTP */
    senderTrackerInit();

    uint8_t frameA[DMX_SLOT_COUNT];
    uint8_t frameB[DMX_SLOT_COUNT];
    for (int i = 0; i < DMX_SLOT_COUNT; i++) {
        frameA[i] = 10;
        frameB[i] = 30;
    }
    frameA[5] = 100;
    frameB[10] = 200;

    updateSender(0x0100007F, PROTO_ARTNET, TEST_UNIVERSE, 100, frameA, DMX_SLOT_COUNT);
    updateSender(0x0200007F, PROTO_ARTNET, TEST_UNIVERSE, 100, frameB, DMX_SLOT_COUNT);

    int slotA = find_sender_by_ip(0x0100007F);
    int slotB = find_sender_by_ip(0x0200007F);
    ASSERT(slotA >= 0 && slotB >= 0);

    uint32_t nowMs = g_senders[slotA].lastMs;
    if (g_senders[slotB].lastMs > nowMs) nowMs = g_senders[slotB].lastMs;
    mergeOutput(0, nowMs);

    for (int c = 0; c < DMX_SLOT_COUNT; c++) {
        uint8_t expected = (frameA[c] > frameB[c]) ? frameA[c] : frameB[c];
        ASSERT(g_dmxBufState.buffers[0].data[c + 1] == expected);
    }
}

TEST(merge_ltp_uses_last_seen) {
    memset(&cfg, 0, sizeof(cfg));
    memset(&g_dmxBufState, 0, sizeof(g_dmxBufState));

    cfg.outputs[0].en = 1;
    cfg.outputs[0].uni = 1;
    cfg.outputs[0].mergemode = 2;  /* MERGE_LTP */
    senderTrackerInit();

    uint8_t frameA[DMX_SLOT_COUNT];
    uint8_t frameB[DMX_SLOT_COUNT];
    for (int i = 0; i < DMX_SLOT_COUNT; i++) {
        frameA[i] = 10;
        frameB[i] = 30;
    }

    updateSender(0x0100007F, PROTO_ARTNET, TEST_UNIVERSE, 100, frameA, DMX_SLOT_COUNT);
    updateSender(0x0200007F, PROTO_ARTNET, TEST_UNIVERSE, 100, frameB, DMX_SLOT_COUNT);

    int slotA = find_sender_by_ip(0x0100007F);
    int slotB = find_sender_by_ip(0x0200007F);
    ASSERT(slotA >= 0 && slotB >= 0);

    /* Force sender B to be strictly "last seen" so LTP picks it. */
    g_senders[slotA].lastMs = 100;
    g_senders[slotB].lastMs = 200;
    mergeOutput(0, 200);

    for (int c = 0; c < DMX_SLOT_COUNT; c++) {
        ASSERT(g_dmxBufState.buffers[0].data[c + 1] == frameB[c]);
    }
}

int main(void) {
    printf("=== Merge Engine Tests ===\n\n");
    run_merge_htp_selects_highest_per_channel();
    run_merge_ltp_uses_last_seen();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
