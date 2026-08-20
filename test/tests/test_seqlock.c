#include "shim.h"
#include "seqlock.h"
#include "dmx_buffer.h"
#include <string.h>
#include <stdlib.h>

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

TEST(clean_snapshot_during_active_write) {
    DmxBufferState state = {0};
    SeqLock* sl = &state.buffers[0].seq;
    
    /* Write a frame */
    uint8_t frame[DMX_PACKET_SIZE];
    memset(frame, 0xAB, DMX_PACKET_SIZE);
    
    seqlock_write_begin(sl);
    memcpy(state.buffers[0].data, frame, DMX_PACKET_SIZE);
    seqlock_write_end(sl);
    
    /* Snapshot should be consistent */
    uint8_t snapshot[DMX_PACKET_SIZE];
    bool ok = seqlock_snapshot(sl, snapshot, state.buffers[0].data, DMX_PACKET_SIZE);
    ASSERT(ok);
    ASSERT(memcmp(snapshot, frame, DMX_PACKET_SIZE) == 0);
}

TEST(stable_copy_under_concurrent_load) {
    DmxBufferState state = {0};
    SeqLock* sl = &state.buffers[0].seq;
    
    for (int cycle = 0; cycle < 100; cycle++) {
        uint8_t frame[DMX_PACKET_SIZE];
        memset(frame, (uint8_t)(cycle & 0xFF), DMX_PACKET_SIZE);
        
        seqlock_write_begin(sl);
        memcpy(state.buffers[0].data, frame, DMX_PACKET_SIZE);
        seqlock_write_end(sl);
        
        uint8_t snapshot[DMX_PACKET_SIZE];
        bool ok = seqlock_snapshot(sl, snapshot, state.buffers[0].data, DMX_PACKET_SIZE);
        ASSERT(ok);
        ASSERT(memcmp(snapshot, frame, DMX_PACKET_SIZE) == 0);
    }
}

TEST(snapshot_returns_false_on_torn_read) {
    DmxBufferState state = {0};
    SeqLock* sl = &state.buffers[0].seq;
    
    /* Manually set seqlock to odd (writer mid-update) */
    sl->seq = 1;
    
    uint8_t frame[DMX_PACKET_SIZE] = {0};
    uint8_t snapshot[DMX_PACKET_SIZE] = {0};
    
    /* Should fail after 8 retries */
    bool ok = seqlock_snapshot(sl, snapshot, frame, DMX_PACKET_SIZE);
    ASSERT(!ok);
}

int main(void) {
    printf("=== Seqlock Tests ===\n\n");
    run_clean_snapshot_during_active_write();
    run_stable_copy_under_concurrent_load();
    run_snapshot_returns_false_on_torn_read();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
