#include <stdio.h>
#include <string.h>

#include "artnet.h"
#include "artnet_fixture.h"

static int tests_run;
static int tests_passed;

static struct {
    int standard_calls;
    int nzs_calls;
    int sync_calls;
    int bridge_calls;
    uint16_t universe;
    uint16_t length;
    uint8_t priority;
    uint8_t start_code;
    uint8_t proto;
    uint32_t source_ip;
    uint8_t payload[LUX_ARTDMX_MAX_SLOTS];
} capture;

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

static void reset_capture(void) {
    memset(&capture, 0, sizeof(capture));
}

void routeFrame(uint16_t universe, const uint8_t* data, uint16_t length,
                uint32_t senderIp, uint8_t proto, uint8_t priority) {
    capture.standard_calls++;
    capture.universe = universe;
    capture.length = length;
    capture.source_ip = senderIp;
    capture.proto = proto;
    capture.priority = priority;
    if (length > sizeof(capture.payload)) {
        length = sizeof(capture.payload);
    }
    if (length > 0 && data) {
        memcpy(capture.payload, data, length);
    }
}

void routeFrameNzs(uint16_t universe, const uint8_t* data, uint16_t length,
                   uint8_t startCode, uint32_t senderIp, uint8_t priority) {
    capture.nzs_calls++;
    capture.universe = universe;
    capture.length = length;
    capture.start_code = startCode;
    capture.source_ip = senderIp;
    capture.priority = priority;
    if (length > sizeof(capture.payload)) {
        length = sizeof(capture.payload);
    }
    if (length > 0 && data) {
        memcpy(capture.payload, data, length);
    }
}

void flushArtSyncStaged(void) {
    capture.sync_calls++;
}

void bridgeDispatch(uint16_t opcode, const uint8_t* packet, uint16_t length,
                    uint32_t sourceIp) {
    (void)opcode;
    (void)packet;
    (void)length;
    (void)sourceIp;
    capture.bridge_calls++;
}

TEST(dispatches_artdmx_to_standard_router) {
    const uint8_t slots[] = {0x11, 0x22, 0x33};
    lux_artnet_fixture_t fixture;
    reset_capture();
    ASSERT(lux_artnet_fixture_init(&fixture, 0x1234, 9, 180,
                                   slots, sizeof(slots)));
    ASSERT(artnet_dispatch_packet(fixture.packet, fixture.packet_length,
                                  0xc0a8010a));
    ASSERT(capture.standard_calls == 1);
    ASSERT(capture.universe == 0x1234);
    ASSERT(capture.length == sizeof(slots));
    ASSERT(capture.source_ip == 0xc0a8010a);
    ASSERT(capture.priority == 180);
    ASSERT(memcmp(capture.payload, slots, sizeof(slots)) == 0);
}

TEST(clamps_declared_artdmx_length_to_received_payload) {
    lux_artnet_fixture_t fixture;
    const uint8_t slot = 0x7f;
    reset_capture();
    ASSERT(lux_artnet_fixture_init(&fixture, 2, 1, 100, &slot, 1));
    fixture.packet[16] = 0x02;
    fixture.packet[17] = 0x00; /* claim 512 slots; only one arrived */
    ASSERT(artnet_dispatch_packet(fixture.packet, fixture.packet_length, 1));
    ASSERT(capture.standard_calls == 1);
    ASSERT(capture.length == 1);
    ASSERT(capture.payload[0] == slot);
}

TEST(dispatches_artnzs_and_sync_and_bridge) {
    lux_artnet_fixture_t fixture;
    uint8_t packet[ARTNET_MAX_PACKET];
    reset_capture();
    ASSERT(lux_artnet_fixture_init(&fixture, 3, 2, 90, NULL, 0));
    fixture.packet[8] = 0x00;
    fixture.packet[9] = 0x58; /* ArtNzs */
    fixture.packet[16] = 0;
    fixture.packet[17] = 1;
    fixture.packet[60] = 0xf0;
    fixture.packet[61] = 0xaa;
    fixture.packet_length = 62;
    ASSERT(artnet_dispatch_packet(fixture.packet, fixture.packet_length, 2));
    ASSERT(capture.nzs_calls == 1);
    ASSERT(capture.start_code == 0xf0);
    ASSERT(capture.length == 1);
    ASSERT(capture.payload[0] == 0xaa);

    memcpy(packet, fixture.packet, sizeof(packet));
    packet[8] = 0x00;
    packet[9] = 0x53; /* ArtSync */
    ASSERT(artnet_dispatch_packet(packet, 18, 2));
    ASSERT(capture.sync_calls == 1);

    packet[8] = 0x00;
    packet[9] = 0x20; /* ArtPoll */
    ASSERT(artnet_dispatch_packet(packet, 18, 2));
    ASSERT(capture.bridge_calls == 1);
}

TEST(rejects_null_short_and_unknown_packets) {
    uint8_t packet[18] = {0};
    reset_capture();
    ASSERT(!artnet_dispatch_packet(NULL, sizeof(packet), 1));
    ASSERT(!artnet_dispatch_packet(packet, 9, 1));
    packet[8] = 0xff;
    packet[9] = 0xff;
    ASSERT(!artnet_dispatch_packet(packet, sizeof(packet), 1));
    ASSERT(capture.standard_calls == 0);
    ASSERT(capture.nzs_calls == 0);
    ASSERT(capture.bridge_calls == 0);
}

int main(void) {
    printf("=== Art-Net Dispatch Tests ===\n\n");
    run_dispatches_artdmx_to_standard_router();
    run_clamps_declared_artdmx_length_to_received_payload();
    run_dispatches_artnzs_and_sync_and_bridge();
    run_rejects_null_short_and_unknown_packets();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
