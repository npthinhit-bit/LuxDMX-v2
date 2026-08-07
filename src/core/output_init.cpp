#include "output_init.h"
#include "dmx_rmt.h"
#include "config_enums.h"
#include "driver/rmt_tx.h"
#include <Arduino.h>

void sanitizeOutputs() {
    for (int i = 0; i < MAX_OUTPUTS; i++)
        if (cfg.outputs[i].enabled && cfg.outputs[i].txPin < 0)
            cfg.outputs[i].enabled = false;
}

dmx_output_t g_outputs[MAX_OUTPUTS] = {};
bool         outReady[MAX_OUTPUTS] = {false};
bool         dmxReady = false;
int          monitorOut = 0;
int          rdmOut = -1;
int          rdmLineForOut[MAX_OUTPUTS] = {-1, -1, -1, -1};
int          rdmOutForLine[MAX_OUTPUTS] = {-1, -1, -1, -1};

int viewOutput() {
    if (monitorOut >= 0 && monitorOut < MAX_OUTPUTS && cfg.outputs[monitorOut].enabled)
        return monitorOut;
    for (int i = 0; i < MAX_OUTPUTS; i++) if (cfg.outputs[i].enabled) return i;
    return 0;
}

void rdmOutSelect(int outIdx) {
    if (outIdx < 0 || outIdx >= MAX_OUTPUTS) return;
    int line = rdmLineForOut[outIdx];
    if (line >= 0) rdmRmtSelect(line);
}

void outputInitAll() {
    dmxReady = false;
    rdmOut   = -1;
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        rdmLineForOut[i] = -1;
        rdmOutForLine[i] = -1;
        outReady[i] = false;
        g_outputs[i].ready = false;
        g_outputs[i].rmt.chan = nullptr;
        g_outputs[i].rmt.sym  = nullptr;
    }
    monitorOut = 0;
    bool firstEnabled = true;
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (!cfg.outputs[i].enabled) continue;
        if (cfg.outputs[i].txPin < 0) {
            Serial.printf("[DMX] out%d skipped: enabled but no TX pin (tx=%d)\n",
                          i, cfg.outputs[i].txPin);
            continue;
        }
        bool dup = false;
        for (int j = 0; j < i; j++)
            if (outReady[j] && cfg.outputs[j].port == cfg.outputs[i].port
                && cfg.outputs[j].rtsPin >= 0 && cfg.outputs[i].rtsPin >= 0) dup = true;
        if (dup) {
            Serial.printf("[DMX] out%d skipped: RDM UART port %d already in use\n",
                          i, cfg.outputs[i].port);
            continue;
        }
        output_mode_t mode = resolveOutputMode(cfg.outputs[i].mode, cfg.outputs[i].rtsPin);
        uart_port_t uart = UART_NUM_MAX;
        if (mode == OUTPUT_MODE_RDM_FULL) {
            if (cfg.outputs[i].port == 1)       uart = UART_NUM_1;
            else if (cfg.outputs[i].port == 2)  uart = UART_NUM_2;
        }
        int rmtCh = i;
        if (rmtDmxInit(&g_outputs[i].rmt, cfg.outputs[i].txPin, rmtCh)) {
            g_outputs[i].ready = true;
            g_outputs[i].index = i;
            g_outputs[i].rmtChannel = rmtCh;
            g_outputs[i].mode = mode;
            g_outputs[i].dePin = cfg.outputs[i].rtsPin;
            g_outputs[i].rxPin = cfg.outputs[i].rxPin;
            g_outputs[i].uartPort = uart;
            outReady[i] = true; dmxReady = true;
            if (firstEnabled) { monitorOut = i; firstEnabled = false; }
            if (mode == OUTPUT_MODE_RDM_FULL) {
                int line = rdmRmtInit(&g_outputs[i].rmt, cfg.outputs[i].rtsPin,
                                      cfg.outputs[i].rxPin, uart);
                if (line >= 0) {
                    rdmLineForOut[i] = line;
                    rdmOutForLine[line] = i;
                    if (rdmOut < 0) rdmOut = i;
                }
            }
            Serial.printf("[DMX] out%d ready (RMT ch%d%s): uni=%d tx=%d mode=%d\n",
                          i, rmtCh, mode == OUTPUT_MODE_RDM_FULL ? "+RDM" : "",
                          cfg.outputs[i].universe, cfg.outputs[i].txPin, (int)mode);
        } else Serial.printf("[DMX] out%d RMT init FAILED\n", i);
    }
    if (!dmxReady) Serial.println("[DMX] no outputs enabled");
    else Serial.printf("[DMX] ready (monitor=out%d rdm=out%d)\n", monitorOut, rdmOut);
}

bool dmxIsDelta(int outIdx) {
    return cfg.outputs[outIdx].txStyle == TXSTYLE_DELTA;
}
