#include "websocket.h"
#include "ws_frame.h"
#include "stats.h"
#include "sender_tracker.h"
#include "web_server.h"   // sendersJson, logJson
#include <Arduino.h>

AsyncWebSocket ws("/ws");

void wsPush() {
    wsBuildFrame();
    if (ws.availableForWriteAll()) {
        try { ws.binaryAll(wsBuf, WS_FRAME_LEN); } catch (...) {}
    }
}

void wsPushMeta() {
    if (ws.count() == 0 || !ws.availableForWriteAll()) return;
    if (ESP.getFreeHeap() < 40000 || ESP.getMaxAllocHeap() < 24000) return;
    try {
        String m = "{\"meta\":1,\"senders\":";
        m += sendersJson();
        m += ",\"log\":";
        m += logJson();
        m += "}";
        ws.textAll(m);
    } catch (...) {}
}

void wsInit(AsyncWebServer& srv) {
    ws.onEvent([](AsyncWebSocket* server, AsyncWebSocketClient* client,
                  AwsEventType type, void* arg, void* data, size_t len) {
        if (type == WS_EVT_CONNECT) {
            Serial.printf("[WS] client %u connected\n", client->id());
        } else if (type == WS_EVT_DISCONNECT) {
            Serial.printf("[WS] client %u disconnected\n", client->id());
        } else if (type == WS_EVT_DATA) {
            if (len > 0 && data) handleWsText((const char*)data, len);
        }
    });
    srv.addHandler(&ws);
}
