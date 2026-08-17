#include "websocket.h"
#include "sender_tracker.h"
#include "stats.h"
#include "web_server.h"  // sendersJson, logJson
#include "ws_frame.h"
#include <Arduino.h>

AsyncWebSocket ws("/ws");

// Per-slot client tracking (core 0 only — no cross-core race).
static AsyncWebSocketClient* wsClients[WS_MAX_CLIENTS]        = {nullptr};
static uint32_t              wsClientFrameSeq[WS_MAX_CLIENTS] = {0};

void wsPush()
{
    if (ws.count() == 0)
        return;
    wsBuildFrame();
    // Push to each connected client individually, respecting per-client
    // subscriptions and delta detection.
    for (int i = 0; i < WS_MAX_CLIENTS; i++)
    {
        AsyncWebSocketClient* c = wsClients[i];
        if (!c)
            continue;
        if (c->status() != WS_CONNECTED)
        {
            wsClients[i] = nullptr;
            continue;
        }
        uint16_t sub     = wsClientSub[i];
        uint16_t changed = wsChangedBitmap & sub;
        // Skip if no subscribed universes changed AND this client already saw this frame.
        if (changed == 0 && wsClientFrameSeq[i] == wsFrameSeq)
            continue;
        wsClientFrameSeq[i] = wsFrameSeq;
        if (!c->canSend())
            continue;
        try
        {
            c->binary((const char*)wsBuf, WS_FRAME_LEN);
        }
        catch (...)
        {
        }
    }
}

void wsPushMeta()
{
    // Meta push stays as-is (2 Hz, text, all clients).
    if (ws.count() == 0 || !ws.availableForWriteAll())
        return;
    if (ESP.getFreeHeap() < 40000 || ESP.getMaxAllocHeap() < 24000)
        return;
    try
    {
        String m = "{\"meta\":1,\"senders\":";
        m += sendersJson();
        m += ",\"log\":";
        m += logJson();
        m += "}";
        ws.textAll(m);
    }
    catch (...)
    {
    }
}

void wsInit(AsyncWebServer& srv)
{
    ws.onEvent(
        [](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, void* data, size_t len)
        {
            uint32_t id   = client->id();
            uint32_t slot = id % WS_MAX_CLIENTS;
            if (type == WS_EVT_CONNECT)
            {
                wsClients[slot]        = client;
                wsClientSub[slot]      = (1 << MAX_OUTPUTS) - 1;  // all universes by default
                wsClientFrameSeq[slot] = 0;                       // forces initial frame send
                Serial.printf("[WS] client %u connected (slot %u, sub 0x%04x)\n", id, slot, wsClientSub[slot]);
            }
            else if (type == WS_EVT_DISCONNECT)
            {
                if (wsClients[slot] == client)
                    wsClients[slot] = nullptr;
                wsClientSub[slot] = 0;
                Serial.printf("[WS] client %u disconnected (slot %u)\n", id, slot);
            }
            else if (type == WS_EVT_DATA)
            {
                if (len > 0 && data)
                    handleWsText((const char*)data, len, id);
            }
        });
    srv.addHandler(&ws);
}