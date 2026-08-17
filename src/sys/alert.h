#pragma once
#include "config_schema.h"
#include <Arduino.h>

// Webhook alert client — POSTs JSON to a configurable URL when DMX source is lost.
// Called from merge_engine or main loop when a port goes dark.
void alertSourceLost(int outIdx, const char* sourceIp);
void alertSourceRestored(int outIdx);
