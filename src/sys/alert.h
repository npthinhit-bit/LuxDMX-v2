#pragma once
#include <Arduino.h>
#include "config_schema.h"

// Webhook alert client — POSTs JSON to a configurable URL when DMX source is lost.
// Called from merge_engine or main loop when a port goes dark.
void alertSourceLost(int outIdx, const char* sourceIp);
void alertSourceRestored(int outIdx);

// Email alert stub — logs the alert locally. Full SMTP transport is a future
// enhancement; this stub ensures the API is present for integration.
void alertEmailSourceLost(int outIdx, const char* sourceIp);
void alertEmailSourceRestored(int outIdx);
