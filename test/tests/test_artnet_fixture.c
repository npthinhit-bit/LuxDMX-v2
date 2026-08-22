#include <stdio.h>
#include <string.h>

#include "artnet_fixture.h"

static int tests_run;
static int tests_passed;

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
} while (0)

TEST(builds_artdmx_packet) {
    const uint8_t slots[] = {0x00, 0x12, 0xa5, 0xff};
    lux_artnet_fixture_t fixture;
    ASSERT(lux_artnet_fixture_init(&fixture, 0x1234, 7, 200,
                                   slots, sizeof(slots)));
    ASSERT(lux_test_fixture_contract_is_valid(&fixture.contract));
    ASSERT(fixture.packet_length == LUX_ARTDMX_DATA_OFFSET + sizeof(slots));
    ASSERT(memcmp(fixture.packet, ARTNET_ID, 8) == 0);
    ASSERT(fixture.packet[8] == 0x00 && fixture.packet[9] == 0x50);
    ASSERT(fixture.packet[10] == 0x00 && fixture.packet[11] == 0x0e);
    ASSERT(fixture.packet[12] == 7);
    ASSERT(fixture.packet[14] == 0x34 && fixture.packet[15] == 0x12);
    ASSERT(fixture.packet[16] == 0x00 && fixture.packet[17] == sizeof(slots));
    ASSERT(fixture.packet[59] == 200);
    ASSERT(memcmp(&fixture.packet[LUX_ARTDMX_DATA_OFFSET], slots,
                  sizeof(slots)) == 0);
}

TEST(allows_zero_length_payload) {
    lux_artnet_fixture_t fixture;
    ASSERT(lux_artnet_fixture_init(&fixture, 1, 0, 100, NULL, 0));
    ASSERT(fixture.packet_length == LUX_ARTDMX_DATA_OFFSET);
    ASSERT(fixture.packet[16] == 0 && fixture.packet[17] == 0);
}

TEST(rejects_invalid_payload) {
    lux_artnet_fixture_t fixture;
    uint8_t one_slot = 1;
    ASSERT(!lux_artnet_fixture_init(NULL, 1, 1, 1, &one_slot, 1));
    ASSERT(!lux_artnet_fixture_init(&fixture, 1, 1, 1, NULL, 1));
    ASSERT(!lux_artnet_fixture_init(&fixture, 1, 1, 1, &one_slot,
                                    LUX_ARTDMX_MAX_SLOTS + 1));
}

int main(void) {
    printf("=== ArtDMX Fixture Tests ===\n\n");
    run_builds_artdmx_packet();
    run_allows_zero_length_payload();
    run_rejects_invalid_payload();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
