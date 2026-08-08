#pragma once
#include <stdint.h>
#include <stddef.h>

class String;

// Process a WebSocket text frame from the browser. Handles viewout/blackout/
// mode/identify/set commands, and RDM control (discover, setaddr, identify,
// setpers, setlabel). Writes into the shared state that loop() consumes.
void handleWsText(const char* payload, size_t len);

// RDM command handling (discover, setaddr, identify, setpers, setlabel).
// Called from handleWsText after the basic commands.
void handleWsTextRdm(const String& msg);

// Execute queued RDM operations from the WebSocket handler.
// Called from loop() after the DMX task releases the RMT channel.
void rdmWsProcessQueued();
