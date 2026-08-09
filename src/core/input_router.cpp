#include "input_router.h"
#include "dmx_input.h"
#include "frame_router.h"
#include "artnet.h"
#include "sacn.h"
#include "sender_tracker.h"
#include "config_schema.h"
#include "output_init.h"
#include <Arduino.h>

extern DmxInFrame g_dmxInFrame;

void inputRouterPoll() {
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        const DmxOutput& out = cfg.outputs[i];
        if (!out.enabled) continue;
        if (out.inputMode == DMX_IN_OFF) continue;
        if (out.port < 1 || out.port > 2) continue;
        if (!outReady[i]) continue;

        if (dmxInPoll(out.port)) {
            // DMX-in frame ready: retransmit to network
            if (out.inputMode == DMX_IN_TO_NET) {
                int portAddr = (int)portAddress(out);
                // Route the received DMX frame into the network pipeline
                // with default priority 100, protocol Art-Net
                updateSender(0, 0, portAddr, 100, g_dmxInFrame.data, g_dmxInFrame.len);
                routeFrame(portAddr, g_dmxInFrame.data, g_dmxInFrame.len, 0, 0, 100);
            }
            // DMX_IN_MONITOR: frame is already in dmxInFrame, web UI can read it
        }
    }
}
