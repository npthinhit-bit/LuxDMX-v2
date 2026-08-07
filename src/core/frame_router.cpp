#include "frame_router.h"
#include "sender_tracker.h"
#include "merge_engine.h"
#include "dmx_buffer.h"
#include "stats.h"
#include "config_schema.h"
#include "output_init.h"   // viewOutput
#include <Arduino.h>

extern void maybeLog(int outIdx, const uint8_t* cur, uint16_t len, uint32_t ip, uint8_t proto);

void routeFrame(int artUniverse, const uint8_t* data, uint16_t length,
                uint32_t senderIp, uint8_t proto, uint8_t priority) {
    updateSender(senderIp, proto, (int16_t)artUniverse, priority, data, length);

    bool matched = false;
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (!cfg.outputs[i].enabled || cfg.outputs[i].universe != artUniverse) continue;
        mergeOutput(i);
        if (i == viewOutput()) maybeLog(i, &dmxBuffers[i].data[1], 512, senderIp, proto);
        matched = true;
    }
    if (!matched) return;
    g_srcStatus = sourceStatus();
}
