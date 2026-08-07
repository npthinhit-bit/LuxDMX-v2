#include "ws_handler.h"
#include "stats.h"
#include "output_init.h"
#include "dmx_buffer.h"
#include "rdm_engine.h"
#include "config_schema.h"
#include <Arduino.h>

extern uint16_t identifyCh;
extern uint32_t identifyUntil;
void handleWsTextRdm(const String& /*msg*/) {}

void handleWsText(const char* payload, size_t len) {
    String msg(payload, len);
    if (msg.indexOf("\"viewout\"") >= 0) {
        int k = msg.indexOf("\"out\":");
        if (k >= 0) {
            int o = msg.substring(k + 6).toInt();
            if (o >= 0 && o < MAX_OUTPUTS && cfg.outputs[o].enabled) monitorOut = o;
        }
        return;
    }
    if (msg.indexOf("\"blackout\"") >= 0) {
        const int mo = viewOutput();
        dmxBufWriteBegin(mo); memset(&dmxBuffers[mo].data[1], 0, 512); dmxBufWriteEnd(mo);
        return;
    }
    if (msg.indexOf("\"mode\"") >= 0) {
        manualMode = (msg.indexOf("true") >= 0); return;
    }
    if (msg.indexOf("\"identify\"") >= 0) {
        int chIdx = msg.indexOf("\"ch\":");
        if (chIdx < 0) return;
        int ch = msg.substring(chIdx + 5).toInt();
        if (ch < 1 || ch > 512) return;
        identifyCh = (uint16_t)ch;
        identifyUntil = millis() + IDENTIFY_MS;
        return;
    }
    if (msg.indexOf("\"set\"") >= 0) {
        int chIdx  = msg.indexOf("\"ch\":");
        int valIdx = msg.indexOf("\"val\":");
        if (chIdx < 0 || valIdx < 0) return;
        int ch  = msg.substring(chIdx  + 5).toInt();
        int val = msg.substring(valIdx + 6).toInt();
        if (ch < 1 || ch > 512) return;
        const int mo = viewOutput();
        dmxBufWriteBegin(mo);
        dmxBufWriteEndSet(mo, ch, (uint8_t)constrain(val, 0, 255));
        return;
    }
    handleWsTextRdm(msg);
}
