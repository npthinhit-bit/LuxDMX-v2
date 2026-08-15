#include "frame_router.h"
#include "sender_tracker.h"
#include "merge_engine.h"
#include "dmx_buffer.h"
#include "stats.h"
#include "config_schema.h"
#include "output_init.h"   // viewOutput
#include <Arduino.h>

extern void maybeLog(int outIdx, const uint8_t* cur, uint16_t len, uint32_t ip, uint8_t proto);

static void routeFrameImpl(int universe, const uint8_t* data, uint16_t length,
                           uint8_t startCode, uint32_t senderIp, uint8_t proto, uint8_t priority) {
    updateSender(senderIp, proto, (int16_t)universe, priority, data, length);
    bool matched = false;
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (!cfg.outputs[i].enabled || portAddress(cfg.outputs[i]) != (uint16_t)universe) continue;
        stats().rxFrameCount[i]++;
        dmxBufWriteBegin(i);
        dmxBufferState().buffers[i].data[0] = startCode;
        memcpy(&dmxBufferState().buffers[i].data[1], data, length > 512 ? 512 : length);
        dmxBufWriteEnd(i);
        mergeOutput(i);
        if (i == viewOutput()) maybeLog(i, &dmxBufferState().buffers[i].data[1], 512, senderIp, proto);
        // Universe splitting: mirror to additional outputs via splitMask
        if (cfg.outputs[i].splitMask) {
            for (int j = 0; j < MAX_OUTPUTS; j++) {
                if (!(cfg.outputs[i].splitMask & (1 << j))) continue;
                if (!cfg.outputs[j].enabled || j == i) continue;
                dmxBufWriteBegin(j);
                dmxBufferState().buffers[j].data[0] = startCode;
                memcpy(&dmxBufferState().buffers[j].data[1], data, length > 512 ? 512 : length);
                dmxBufWriteEnd(j);
                mergeOutput(j);
            }
        }
        matched = true;
    }
    if (matched) stats().srcStatus = sourceStatus();
}

void routeFrame(int artUniverse, const uint8_t* data, uint16_t length,
                uint32_t senderIp, uint8_t proto, uint8_t priority) {
    routeFrameImpl(artUniverse, data, length, 0, senderIp, proto, priority);
}

void routeFrameNzs(int artUniverse, uint8_t* data, uint16_t length,
                   uint8_t startCode, uint32_t senderIp, uint8_t priority) {
    routeFrameImpl(artUniverse, data, length, startCode, senderIp, 0, priority);
}
