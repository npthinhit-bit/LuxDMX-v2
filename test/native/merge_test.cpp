// Host test for the merge engine (src/core/).
// Verifies HTP merge, LTP/off last-frame-wins, and signal-loss/zero behavior.
// Note: mergeOutput() calls Arduino millis() and String — both are shimmed.
#include "config_schema.h"
#include "dmx_buffer.h"
#include "merge_engine.h"
#include "sender_tracker.h"
#include <cstdio>
#include <cstring>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg)                 \
    do                                   \
    {                                    \
        if (cond)                        \
            g_pass++;                    \
        else                             \
        {                                \
            g_fail++;                    \
            printf("  FAIL: %s\n", msg); \
        }                                \
    } while (0)

static void resetOutput(int i)
{
    cfg.outputs[i].enabled   = true;
    cfg.outputs[i].universe  = 1;
    cfg.outputs[i].mergeMode = MERGE_OFF;
    cfg.outputs[i].lossMode  = LOSS_HOLD;
}

static void clearSenders()
{
    memset(senderTracker().senders, 0, MAX_SENDERS * sizeof(Sender));
}

int main()
{
    // --- HTP merge: multiple sources, per-channel max ---
    {
        clearSenders();
        memset(&dmxBufferState().buffers[0].data[1], 0, 512);
        resetOutput(0);
        cfg.outputs[0].mergeMode = MERGE_HTP;

        // Two sources, same universe, different channels.
        uint8_t f1[512];
        memset(f1, 100, sizeof(f1));
        uint8_t f2[512];
        memset(f2, 200, sizeof(f2));
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
        memset(&dmxBufferState().buffers[0].data[1], 0, 512);
        resetOutput(0);
        cfg.outputs[0].mergeMode = MERGE_OFF;

        uint8_t f1[512];
        memset(f1, 50, sizeof(f1));
        uint8_t f2[512];
        memset(f2, 200, sizeof(f2));
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
        memset(&dmxBufferState().buffers[0].data[1], 0, 512);
        resetOutput(0);
        cfg.outputs[0].lossMode  = LOSS_ZERO;
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
        memset(&dmxBufferState().buffers[0].data[1], 0, 512);
        resetOutput(0);
        cfg.outputs[0].mergeMode = MERGE_OFF;

        uint8_t f[512];
        memset(f, 123, sizeof(f));
        updateSender(0xC0A80101, 0, 2, 100, f, 512);  // universe 2, output listens on 1

        mergeOutput(0);
        uint8_t snap[DMX_PACKET_SIZE];
        CHECK(dmxBufSnapshot(0, snap), "diff-univ snapshot ok");
        CHECK(snap[1] == 0, "diff-univ source doesn't contribute");
    }

    // --- LTP-Takeover: highest priority source preempts, not just newest ---
    {
        clearSenders();
        memset(&dmxBufferState().buffers[0].data[1], 0, 512);
        resetOutput(0);
        cfg.outputs[0].mergeMode = MERGE_LTP_TAKEOVER;

        // Low-priority source sends 100, then high-priority sends 200.
        // Even if the low-priority source was "newest", high-prio should win.
        uint8_t f1[512];
        memset(f1, 100, sizeof(f1));
        uint8_t f2[512];
        memset(f2, 200, sizeof(f2));
        // Send low-priority first
        updateSender(0xC0A80103, 0, 1, 100, f1, 512);  // priority 100
        // Manually update lastMs to make it "newer" (simulate timing)
        senderTracker().senders[0].lastMs = 999999;
        updateSender(0xC0A80104, 0, 1, 200, f2, 512);  // priority 200 (higher)
        // senderTracker().senders[1].lastMs will be ~0 (millis shims to 0), senderTracker().senders[0].lastMs is huge

        mergeOutput(0);
        uint8_t snap[DMX_PACKET_SIZE];
        CHECK(dmxBufSnapshot(0, snap), "LTP-Takeover snapshot ok");
        CHECK(snap[1] == 200, "LTP-Takeover picks highest priority even if not newest");
    }

    // --- Priority merge: only highest-priority sources contribute, per-channel max ---
    {
        clearSenders();
        memset(&dmxBufferState().buffers[0].data[1], 0, 512);
        resetOutput(0);
        cfg.outputs[0].mergeMode = MERGE_PRIORITY;

        uint8_t f1[512];
        memset(f1, 100, sizeof(f1));  // priority 100
        uint8_t f2[512];
        memset(f2, 200, sizeof(f2));  // priority 200 (highest)
        uint8_t f3[512];
        memset(f3, 150, sizeof(f3));  // priority 150 (excluded)
        updateSender(0xC0A80101, 0, 1, 100, f1, 512);
        updateSender(0xC0A80102, 0, 1, 200, f2, 512);
        updateSender(0xC0A80103, 0, 1, 150, f3, 512);

        mergeOutput(0);
        uint8_t snap[DMX_PACKET_SIZE];
        CHECK(dmxBufSnapshot(0, snap), "Priority merge snapshot ok");
        CHECK(snap[1] == 200, "Priority merge uses only top-priority source");
    }

    // --- Per-port failsafe timeout: 0 = use global default (SOURCE_TIMEOUT_MS) ---
    {
        clearSenders();
        memset(&dmxBufferState().buffers[0].data[1], 0, 512);
        resetOutput(0);
        cfg.outputs[0].failsafeTimeout = 0;  // should default to SOURCE_TIMEOUT_MS (2500)
        cfg.outputs[0].lossMode        = LOSS_ZERO;

        // Send a frame — source is active (within timeout window).
        uint8_t f[512];
        memset(f, 255, sizeof(f));
        updateSender(0xC0A80101, 0, 1, 100, f, 512);
        mergeOutput(0);
        uint8_t snap[DMX_PACKET_SIZE];
        CHECK(dmxBufSnapshot(0, snap), "failover-timeout-0 snapshot ok");
        CHECK(snap[1] == 255, "source active within timeout window (timeout=0 -> default)");
    }

    // --- Per-port failsafe timeout: non-zero override ---
    {
        clearSenders();
        memset(&dmxBufferState().buffers[0].data[1], 0, 512);
        resetOutput(0);
        cfg.outputs[0].failsafeTimeout = 10;  // 10-second timeout
        cfg.outputs[0].lossMode        = LOSS_ZERO;

        uint8_t f[512];
        memset(f, 200, sizeof(f));
        updateSender(0xC0A80101, 0, 1, 100, f, 512);
        mergeOutput(0);
        uint8_t snap[DMX_PACKET_SIZE];
        CHECK(dmxBufSnapshot(0, snap), "failover-timeout-10 snapshot ok");
        CHECK(snap[1] == 200, "source active within 10s window");
    }

    // --- LOSS_PRESET falls back to HOLD (scene engine not implemented yet) ---
    {
        clearSenders();
        memset(&dmxBufferState().buffers[0].data[1], 0, 512);
        resetOutput(0);
        cfg.outputs[0].lossMode   = LOSS_PRESET;
        cfg.outputs[0].lossPreset = 5;

        mergeOutput(0);
        uint8_t snap[DMX_PACKET_SIZE];
        CHECK(dmxBufSnapshot(0, snap), "LOSS_PRESET snapshot ok");
        CHECK(snap[1] == 0, "LOSS_PRESET falls back to hold (no sources -> buffer stays 0)");
    }

    // --- LOSS_HOME falls back to HOLD ---
    {
        clearSenders();
        memset(&dmxBufferState().buffers[0].data[1], 0, 512);
        resetOutput(0);
        cfg.outputs[0].lossMode = LOSS_HOME;

        mergeOutput(0);
        uint8_t snap[DMX_PACKET_SIZE];
        CHECK(dmxBufSnapshot(0, snap), "LOSS_HOME snapshot ok");
        CHECK(snap[1] == 0, "LOSS_HOME falls back to hold (no sources -> buffer stays 0)");
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
