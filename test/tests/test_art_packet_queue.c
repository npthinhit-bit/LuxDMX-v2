#include "art_packet_queue.h"
#include <stdio.h>
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

static void fill_packet(art_packet_t* pkt, uint16_t length, uint32_t sourceIp, uint8_t fill) {
    memset(pkt, 0, sizeof(*pkt));
    if (length > ART_PKT_MAX_SIZE) {
        length = ART_PKT_MAX_SIZE;
    }
    pkt->length = length;
    pkt->sourceIp = sourceIp;
    memset(pkt->data, fill, length);
}

TEST(push_pop_round_trip) {
    artPktQueueInit();
    ASSERT(artPktQueueEmpty());

    art_packet_t pkt;
    fill_packet(&pkt, 8, 0xC0A8010A, 0x42);
    ASSERT(artPktQueuePush(&pkt));
    ASSERT(!artPktQueueEmpty());

    art_packet_t out;
    ASSERT(artPktQueuePop(&out));
    ASSERT(out.length == 8);
    ASSERT(out.sourceIp == 0xC0A8010A);
    ASSERT(memcmp(out.data, pkt.data, 8) == 0);
    ASSERT(artPktQueueEmpty());
}

TEST(full_ring_drop_policy) {
    artPktQueueInit();
    art_packet_t pkt;
    fill_packet(&pkt, 4, 1, 0x01);

    for (int i = 0; i < ART_PKT_QUEUE_CAPACITY; i++) {
        ASSERT(artPktQueuePush(&pkt));
    }

    /* Queue is now full; the next push must be rejected (drop policy). */
    ASSERT(!artPktQueuePush(&pkt));

    /* Draining must yield exactly ART_PKT_QUEUE_CAPACITY packets. */
    art_packet_t out;
    int count = 0;
    while (artPktQueuePop(&out)) {
        count++;
    }
    ASSERT(count == ART_PKT_QUEUE_CAPACITY);
    ASSERT(artPktQueueEmpty());
}

TEST(empty_ring_drain) {
    artPktQueueInit();
    ASSERT(artPktQueueEmpty());

    art_packet_t out;
    memset(&out, 0xA5, sizeof(out));
    ASSERT(!artPktQueuePop(&out));
    ASSERT(artPktQueueEmpty());
    /* Destination must be left untouched on a failed pop. */
    art_packet_t probe;
    memset(&probe, 0xA5, sizeof(probe));
    ASSERT(memcmp(&out, &probe, sizeof(out)) == 0);
}

int main(void) {
    printf("=== Art Packet Queue Tests ===\n\n");

    run_push_pop_round_trip();
    run_full_ring_drop_policy();
    run_empty_ring_drain();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}

