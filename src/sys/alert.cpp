// Webhook alert module — sends JSON POST on DMX source loss/restore events.
#include "alert.h"
#include "config_schema.h"
#ifdef ESP32
#include <HTTPClient.h>
#include <WiFi.h>
#endif

static bool g_alertSent[MAX_OUTPUTS] = {false};

void alertSourceLost(int outIdx, const char* sourceIp) {
    if (!cfg.webhookAlerts || cfg.webhookUrl.length() == 0) return;
    if (g_alertSent[outIdx]) return;
    g_alertSent[outIdx] = true;

#ifdef ESP32
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    http.setTimeout(3000);
    String payload = "{";
    payload += "\"event\":\"dmx_loss\"";
    payload += ",\"output\":\"" + String(char('A' + outIdx)) + "\"";
    payload += ",\"universe\":" + String(cfg.outputs[outIdx].universe);
    if (sourceIp) payload += ",\"source\":\"" + String(sourceIp) + "\"";
    payload += ",\"uptime_s\":" + String((millis() - 1) / 1000);
    payload += "}";

    http.begin(cfg.webhookUrl);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(payload);
    Serial.printf("[ALERT] source lost on %c (HTTP %d)\n", char('A' + outIdx), code);
    http.end();
#else
    printf("[ALERT] source lost on %c (webhook disabled on host)\n", char('A' + outIdx));
#endif
}

void alertSourceRestored(int outIdx) {
    if (!cfg.webhookAlerts || cfg.webhookUrl.length() == 0) return;
    if (!g_alertSent[outIdx]) return;
    g_alertSent[outIdx] = false;

#ifdef ESP32
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    http.setTimeout(3000);
    String payload = "{";
    payload += "\"event\":\"dmx_restore\"";
    payload += ",\"output\":\"" + String(char('A' + outIdx)) + "\"";
    payload += ",\"universe\":" + String(cfg.outputs[outIdx].universe);
    payload += ",\"uptime_s\":" + String((millis() - 1) / 1000);
    payload += "}";

    http.begin(cfg.webhookUrl);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(payload);
    Serial.printf("[ALERT] source restored on %c (HTTP %d)\n", char('A' + outIdx), code);
    http.end();
#else
    printf("[ALERT] source restored on %c (webhook disabled on host)\n", char('A' + outIdx));
#endif
}
