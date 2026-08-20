#include "dmx_buffer.h"
#include "dmx_rmt_tx.h"
#include "config_engine.h"
#include "boards.h"
#include <string.h>

/* Resolve output_mode from config mode + RTS pin (spec 45 §2) */
int resolveOutputMode(int modeVal, int rtsPin) {
    if (rtsPin >= 0 && modeVal == 1) return 1; /* RDM mode with RTS pin */
    if (rtsPin >= 0 && modeVal == 0) return 0; /* DMX mode with RTS pin */
    return 0; /* DMX-only if no RTS pin */
}

/* Initialize all outputs from config (spec 14 §6.1, 07) */
void outputInitAll(void) {
    memset(dmx_outputs, 0, sizeof(dmx_outputs));
    for (int i = 0; i < MAX_OUTPUTS && i < MAX_OUTPUTS_DRV; i++) {
        DmxOutput* o = &cfg.outputs[i];
        if (!o->en) continue;
        
        dmx_output_t* out = &dmx_outputs[i];
        out->txPin = o->tx;
        out->rxPin = o->rx;
        out->rtsPin = o->rts;
        out->uart = o->port;
        out->mode = resolveOutputMode(o->mode, o->rts);
        out->txRate = o->txrate;
        out->txStyle = o->txstyle;
        out->ready = false;
        
        if (o->tx < 0) continue; /* no TX pin, skip */
        
        if (rmtDmxInit(&out->rmt, o->tx, i)) {
            out->rmt.breakTime = o->brk;
            out->rmt.mabTime = o->mab;
            out->rmt.invert = o->inv;
            out->ready = true;
        }
    }
}

/* Validate output config against hardware constraints */
void sanitizeOutputs(void) {
    for (int i = 0; i < MAX_OUTPUTS && i < MAX_OUTPUTS_DRV; i++) {
        DmxOutput* o = &cfg.outputs[i];
        if (o->tx < 0 && o->rx < 0 && o->rts < 0) {
            o->en = 0; /* disable outputs with no pins */
        }
        if (o->uni < 0) o->uni = 0;
        if (o->uni > 15) o->uni = 15;
        if (o->brk < 88) o->brk = 88;
        if (o->brk > 300) o->brk = 300;
        if (o->mab < 0) o->mab = 0;
        if (o->mab > 300) o->mab = 300;
    }
}