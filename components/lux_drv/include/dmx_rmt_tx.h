#pragma once
#include "driver/rmt_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RMT_DMX_MAX_SYM 3000
#define DMX_RMT_CLOCK_HZ 1000000
#define DMX_BREAK_US 176
#define DMX_MAB_US 12

typedef struct {
    rmt_channel_handle_t chan;
    rmt_encoder_handle_t enc;
    rmt_symbol_word_t* sym;
    int nsym;
    int channel;
    bool hasDma;
    int breakTime;
    int mabTime;
    int invert;
} RmtDmx;

typedef struct {
    RmtDmx rmt;
    int dePin;
    int uart;
    int mode;
    bool ready;
    int txPin;
    int rxPin;
    int rtsPin;
    int txRate;
    int txStyle;
} dmx_output_t;

#define MAX_OUTPUTS_DRV 4
extern dmx_output_t dmx_outputs[MAX_OUTPUTS_DRV];

bool rmtDmxInit(RmtDmx* r, int gpio, int rmtChannel);
bool rmtDmxIdle(RmtDmx* r);
void rmtDmxKick(RmtDmx* r, const uint8_t* data, int nslots);
void rmtDmxWait(RmtDmx* r);
int rmtDmxEncode(RmtDmx* r, const uint8_t* data, int nslots);

#ifdef __cplusplus
}
#endif
