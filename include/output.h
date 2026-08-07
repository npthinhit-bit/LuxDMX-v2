#pragma once
// Runtime state for a single DMX output, tying together the RMT TX channel
// (dmx_rmt.h), the optional UART RX for RDM (rdm_rmt.h), and the output mode.
// This is the live instance the DMX task owns; it is NOT persisted -- persisted
// settings live in Config.outputs[] (config_schema.h).
#include <Arduino.h>
#include "driver/uart.h"
#include "config_schema.h"
#include "dmx_rmt.h"

enum output_mode_t : uint8_t {
    OUTPUT_MODE_DMX_ONLY = 0,    // RMT TX only, auto-direction RS485, no RDM
    OUTPUT_MODE_RDM_FULL = 1,    // RMT TX + UART RX + DE/RE GPIO, RDM E1.20 capable
};

struct dmx_output_t {
    RmtDmx             rmt;
    int                index;
    int                rmtChannel;
    output_mode_t      mode;
    int                dePin;
    int                rxPin;
    uart_port_t        uartPort;
    bool               ready;
    volatile uint32_t  seq;
};

inline output_mode_t resolveOutputMode(int modeVal, int rtsPin) {
    if (modeVal == OUTPUT_MODE_RDM_FULL) return OUTPUT_MODE_RDM_FULL;
    if (rtsPin >= 0) return OUTPUT_MODE_RDM_FULL;
    return OUTPUT_MODE_DMX_ONLY;
}
