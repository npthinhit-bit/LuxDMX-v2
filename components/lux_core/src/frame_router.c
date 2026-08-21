/*
 * Frame Router implementation
 *
 * updateSender -> per-output portAddress match -> start-code write
 * (seqlock-bracketed) -> mergeOutput -> split-mask mirror.
 *
 * Run on core 0 (per-packet path). mergeOutput() internally brackets its
 * own seqlock transaction on the live DMX buffer.
 */
#include "frame_router.h"
#include "merge_engine.h"
#include "sender_tracker.h"
#include "dmx_buffer.h"
#include "config_engine.h"
#include "esp_timer.h"
#include <string.h>

/* Monotonic uptime in ms — same facility used by sender_tracker.c. */
static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Verbatim copy of a frame into output j's live buffer, including the start
 * code at slot 0. Used for split-mask mirrors that bypass the merge engine. */
static void mirrorSplit(int srcIdx, const uint8_t* data, uint16_t length,
                        uint8_t startCode) {
    int mask = cfg.outputs[srcIdx].splitmask;
    if (!mask) return;

    for (int j = 0; j < MAX_OUTPUTS; j++) {
        if (j == srcIdx) continue;
        if (!(mask & (1 << j))) continue;
        if (!cfg.outputs[j].en) continue;

        uint16_t len = length;
        if (len > DMX_SLOT_COUNT) len = DMX_SLOT_COUNT;

        dmxBufWriteBegin(j);
        g_dmxBufState.buffers[j].data[0] = startCode;
        if (data != NULL && len > 0) {
            memcpy(g_dmxBufState.buffers[j].data + 1, data, len);
        }
        if (len < DMX_SLOT_COUNT) {
            memset(g_dmxBufState.buffers[j].data + 1 + len, 0,
                   DMX_SLOT_COUNT - len);
        }
        dmxBufWriteEnd(j);
    }
}

void routeFrame(uint16_t universe, const uint8_t* data, uint16_t length,
                uint32_t senderIp, uint8_t proto, uint8_t priority) {
    updateSender(senderIp, proto, universe, priority, data, length);

    uint32_t nowMs = now_ms();

    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (!cfg.outputs[i].en) continue;
        if (portAddress(&cfg.outputs[i]) != (int)universe) continue;

        /* Write start code 0x00 to slot 0 (spec 03 §5.1). */
        dmxBufWriteBegin(i);
        g_dmxBufState.buffers[i].data[0] = 0;
        dmxBufWriteEnd(i);

        /* Priority-filtered blend into slots 1..512 (spec 04). */
        mergeOutput(i, nowMs);

        /* Mirror the verbatim frame to split-mask outputs. */
        mirrorSplit(i, data, length, 0);
    }
}

void routeFrameNzs(uint16_t universe, const uint8_t* data, uint16_t length,
                   uint8_t startCode, uint32_t senderIp, uint8_t priority) {
    updateSender(senderIp, PROTO_ARTNET, universe, priority, data, length);

    uint32_t nowMs = now_ms();

    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (!cfg.outputs[i].en) continue;
        if (portAddress(&cfg.outputs[i]) != (int)universe) continue;

        /* Write the non-zero start code to slot 0. */
        dmxBufWriteBegin(i);
        g_dmxBufState.buffers[i].data[0] = startCode;
        dmxBufWriteEnd(i);

        /* Priority-filtered blend into slots 1..512. */
        mergeOutput(i, nowMs);

        /* Mirror verbatim, preserving the start code. */
        mirrorSplit(i, data, length, startCode);
    }
}
