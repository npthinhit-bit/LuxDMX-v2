#pragma once
#include <ESPAsyncWebServer.h>

// Global HTTP server instance (declared in web_server.cpp, used by main.cpp +
// websocket.cpp + artnet.cpp OTA upload handler).
extern AsyncWebServer http;

// HTTP route registration. Called from setup().
void webRegisterRoutes();
void webRegisterRoutes(AsyncWebServer& http);

// HTTP request counter (debug / /info.json).
extern volatile uint32_t httpReqCount;

// JSON builders for the WebSocket meta push and HTTP /senders.json, /log.json.
String sendersJson();
String logJson();
