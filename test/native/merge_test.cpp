// Host test for the merge engine (src/core/).
// Verifies HTP merge, LTP/off last-frame-wins, and signal-loss/zero behavior.
// Note: mergeOutput() calls Arduino millis() and String — both are shimmed.
#include "merge_engine.h"
#include "dmx_buffer.h"
#include "sender_tracker.h"
#include "config_schema.h"
#include <cstdio>
#include <cstring>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("  FAIL: %s\n", msg); } } while (0)

static void resetOutput(int i) {
    cfg.outputs[i].enabled = true;
    cfg.outputs[i].universe = 1;
    cfg.outputs[i].mergeMode = MERGE_OFF;
    cfg.outputs[i].lossMode = LOSS_HOLD;
}

static void clearSenders() {
    memset(senders, 0, MAX_SENDERS * sizeof(Sender));
}

int main() {
    // --- HTP merge: multiple sources, per-channel max ---
    {
        clearSenders();
        memset(&dmxBuffers[0].data[1], 0, 512);
        resetOutput(0);
        cfg.outputs[0].mergeMode = MERGE_HTP;

        // Two sources, same universe, different channels.
        uint8_t f1[512]; memset(f1, 100, sizeof(f1));
        uint8_t f2[512]; memset(f2, 200, sizeof(f2));
        updateSender(0xC0A80101, 0, 1, 100, f1, 512);
        updateSender(0xC0A80102, 0, 1, 100, f2, 512);

        mergeOutput(0);
        uint8_t snap[DMX_PACKET_SIZE];
        CHECK(dmxBufSnapshot(0, snap), "HTP snapshot ok");
        CHECK(snap[1] == 200, "HTP takes max per channel");
    }

    // --- OFF / LTP: last source wins whole frame ---
    {
        clearSenders();
        memset(&dmxBuffers[0].data[1], 0, 512);
        resetOutput(0);
        cfg.outputs[0].mergeMode = MERGE_OFF;

        uint8_t f1[512]; memset(f1, 50, sizeof(f1));
        uint8_t f2[512]; memset(f2, 200, sizeof(f2));
        updateSender(0xC0A80101, 0, 1, 100, f1, 512);
        updateSender(0xC0A80102, 0, 1, 100, f2, 512);

        mergeOutput(0);
        uint8_t snap[DMX_PACKET_SIZE];
        CHECK(dmxBufSnapshot(0, snap), "OFF snapshot ok");
        CHECK(snap[1] == 200, "OFF last-frame-wins");
    }

    // --- Signal loss: LOSS_ZERO blacks the buffer ---
    {
        clearSenders();
        memset(&dmxBuffers[0].data[1], 0, 512);
        resetOutput(0);
        cfg.outputs[0].lossMode = LOSS_ZERO;
        cfg.outputs[0].mergeMode = MERGE_HTP;

        // No sources active -> buffer should be zeroed.
        mergeOutput(0);
        uint8_t snap[DMX_PACKET_SIZE];
        CHECK(dmxBufSnapshot(0, snap), "ZERO snapshot ok");
        CHECK(snap[1] == 0, "ZERO blacks the frame");
    }

    // --- Source on different universe doesn't affect output ---
    {
        clearSenders();
        memset(&dmxBuffers[0].data[1], 0, 512);
        resetOutput(0);
        cfg.outputs[0].mergeMode = MERGE_OFF;

        uint8_t f[512]; memset(f, 123, sizeof(f));
        updateSender(0xC0A80101, 0, 2, 100, f, 512); // universe 2, output listens on 1

        mergeOutput(0);
        uint8_t snap[DMX_PACKET_SIZE];
        CHECK(dmxBufSnapshot(0, snap), "diff-univ snapshot ok");
        CHECK(snap[1] == 0, "diff-univ source doesn't contribute");
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
