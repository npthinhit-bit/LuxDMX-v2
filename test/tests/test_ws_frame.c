#include "ws_frame.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_layout_and_delta(void)
{
    ws_frame_state_t current = {0};
    ws_frame_state_t previous = {0};
    current.fps_x10 = 250;
    current.rssi = -47;
    current.heap = 0x01020304;
    current.uptime = 0xAABBCCDD;
    current.senders = 3;
    current.source_status = 2;
    current.jitter_x10 = 19;
    current.dmx[1][7] = 0x5A;
    current.output_fps_x10[2] = 300;
    current.input_fps_x10[3] = 120;
    current.tx_style[0] = 0x03;
    current.rdm_count = 4;
    current.rdm_sent = 0x10203040;
    current.rdm_received = 0x50607080;

    uint8_t frame[WS_FRAME_LEN];
    assert(wsBuildFrame(frame, &current, &previous) == WS_FRAME_LEN);
    assert(frame[0] == 0x00 && frame[1] == 0xFA);
    assert(frame[2] == 0xFF && frame[3] == 0xD1);
    assert(frame[4] == 0x01 && frame[7] == 0x04);
    assert(frame[WS_DMX_DATA_OFFSET + 512u + 7u] == 0x5A);
    assert(frame[WS_STATS_OFFSET + 4u] == 0x01 && frame[WS_STATS_OFFSET + 5u] == 0x2C);
    assert(frame[WS_STATS_OFFSET + 8u + 6u] == 0x00 && frame[WS_STATS_OFFSET + 8u + 7u] == 0x78);
    assert(frame[WS_CHANGED_OFFSET] == 0x02);
    assert(frame[WS_RDM_TAIL_OFFSET] == 0x00 && frame[WS_RDM_TAIL_OFFSET + 1u] == 0x04);
    assert(frame[WS_RDM_TAIL_OFFSET + 2u] == 0x10 && frame[WS_RDM_TAIL_OFFSET + 5u] == 0x40);
    assert(frame[WS_RDM_TAIL_OFFSET + 6u] == 0x50 && frame[WS_RDM_TAIL_OFFSET + 9u] == 0x80);
}

static void test_push_predicate(void)
{
    assert(wsShouldPush(0x02, 0x02, 5, 4));
    assert(!wsShouldPush(0x02, 0x01, 5, 4));
    assert(!wsShouldPush(0x02, 0x02, 5, 5));
    assert(wsShouldPush(0x00, 0x0F, 6, 5) == false);
}

int main(void)
{
    test_layout_and_delta();
    test_push_predicate();
    puts("ws_frame_test: 2 tests passed");
    return 0;
}
