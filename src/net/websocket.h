#pragma once
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// Global WebSocket server (binary channel for live DMX + meta push).
// Declared here so the web server, loop(), and WS handlers all share one instance.
extern AsyncWebSocket ws;

// Build the binary frame and push it to all WS clients if the async TCP
// write queue has room (bounds memory under a flooded/unreachable client).
void wsPush();

// Push senders JSON + log JSON as a text frame (called at ~2 Hz, not every 40 ms).
void wsPushMeta();

// Called from loop() when a WS text frame arrives from the browser.
// Handles viewout/blackout/mode/identify/set commands, subscribe, and RDM control.
void handleWsText(const char* payload, size_t len, uint32_t clientId);

// Register WS event handler on the AsyncWebServer. Called from setup().
void wsInit(AsyncWebServer& srv);