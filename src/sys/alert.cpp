// Webhook alert module — sends JSON POST on DMX source loss/restore events.
#include "alert.h"
#include "config_schema.h"
#include <HTTPClient.h>
#include <WiFi.h>

static bool g_alertSent[MAX_OUTPUTS] = {false};

void alertSourceLost(int outIdx, const char* sourceIp) {
    if (!cfg.webhookAlerts || cfg.webhookUrl.length() == 0) return;
    if (g_alertSent[outIdx]) return;
    g_alertSent[outIdx] = true;

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
}

void alertSourceRestored(int outIdx) {
    if (!cfg.webhookAlerts || cfg.webhookUrl.length() == 0) return;
    if (!g_alertSent[outIdx]) return;
    g_alertSent[outIdx] = false;

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
}
