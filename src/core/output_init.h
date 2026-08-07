#pragma once
#include "output.h"
#include "rdm_engine.h"

// Runtime state tables, populated by outputInitAll.
extern dmx_output_t g_outputs[];
extern bool         outReady[];
extern bool         dmxReady;
extern int          monitorOut;
extern int          rdmOut;
extern int          rdmLineForOut[];
extern int          rdmOutForLine[];

int  viewOutput();
bool dmxIsDelta(int outIdx);
void sanitizeOutputs();
void outputInitAll();
void rdmOutSelect(int outIdx);
