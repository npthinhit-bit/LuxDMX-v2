#include "web_routes.h"
#include "web_server.h"
#include "web_pages.h"
#include "config_schema.h"
#include "config_core.h"
#include "config_types.h"
#include "firmware_version.h"
#include "sys_platform.h"
#include "stats.h"
#include "sender_tracker.h"
#include "artnet.h"
#include "sacn.h"
#include "dmx_buffer.h"
#include "output_init.h"
#include "rdm_engine.h"
#include "rdm_disc.h"
#include "ethernet.h"
#include "network.h"
#include "led_status.h"
#include "ota.h"
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ETH.h>
#include <WiFi.h>
#include <Preferences.h>

static void sendJson(AsyncWebServerRequest* req, const String& j) {
    AsyncWebServerResponse* r = req->beginResponse(200, "application/json", j);
    r->addHeader("Cache-Control", "no-store");
    req->send(r);
}

void handleDmxJson(AsyncWebServerRequest* req) {
    String j = "{\"outputs\":[";
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (i) j += ",";
        uint8_t frame[DMX_PACKET_SIZE];
        dmxBufSnapshot(i, frame);
        j += "{\"id\":\"";
        j += char('A' + i);
        j += "\",\"universe\":"; j += cfg.outputs[i].universe;
        j += ",\"enabled\":"; j += cfg.outputs[i].enabled ? "true" : "false";
        j += ",\"fps\":" + String(outFpsLive(i), 1);
        j += ",\"srcLost\":" + String(outSrcLost[i] ? "true" : "false");
        j += ",\"txFrames\":" + String(txFrames[i]);
        j += ",\"data\":\"";
        for (int c = 0; c < 512; c++) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02x", frame[c + 1]);
            j += buf;
        }
        j += "\"}";
    }
    j += "]}";
    sendJson(req, j);
}

void handleSendersJson(AsyncWebServerRequest* req) {
    String j = "[";
    uint32_t now = millis();
    for (int i = 0; i < MAX_SENDERS; i++) {
        const Sender& s = senders[i];
        if (s.ip == 0 || now - s.lastMs >= 5000) continue;
        if (j.length() > 1) j += ",";
        j += "{";
        j += "\"ip\":\"" + IPAddress(s.ip).toString() + "\"";
        j += ",\"proto\":" + String(s.proto);
        j += ",\"universe\":" + String(s.universe);
        j += ",\"priority\":" + String(s.priority);
        j += ",\"fps\":" + String(s.fps, 1);
        j += "}";
    }
    j += "]";
    sendJson(req, j);
}

void handleLogJson(AsyncWebServerRequest* req) {
    sendJson(req, logJson());
}

void handleInfoJson(AsyncWebServerRequest* req) {
    String j = "{";
    j += "\"hostname\":\"" + cfg.hostname + "\"";
    j += ",\"version\":\"" + String(FIRMWARE_VERSION) + "\"";
    j += ",\"build\":\"" + String(FIRMWARE_BUILD) + "\"";
    j += ",\"variant\":\"" + String(FIRMWARE_VARIANT) + "\"";
    j += ",\"uptime_s\":" + String(uptimeSec());
    j += ",\"heap_free\":" + String(ESP.getFreeHeap());
    j += ",\"rssi\":" + String(netRSSI());
    j += ",\"eth\":" + String(g_useEth ? "true" : "false");
    if (g_useEth) {
        j += ",\"eth_ip\":\"" + ETH.localIP().toString() + "\"";
        j += ",\"eth_speed\":" + String(ETH.linkSpeed());
    }
    j += ",\"protocol\":" + String(cfg.protocol);
    j += ",\"board\":\"" + String(BOARD_ID) + "\"";
    j += ",\"mcu\":\"" + String(MCU_ID) + "\"";
    j += ",\"http_reqs\":" + String(httpReqCount);
    j += "}";
    sendJson(req, j);
}

void handleVersionJson(AsyncWebServerRequest* req) {
    String j = "{";
    j += "\"version\":\"" + String(FIRMWARE_VERSION) + "\"";
    j += ",\"build\":\"" + String(FIRMWARE_BUILD) + "\"";
    j += ",\"variant\":\"" + String(FIRMWARE_VARIANT) + "\"";
    j += ",\"latest\":\"" + latestVersion + "\"";
    j += ",\"updateAvailable\":" + String(updateAvailable ? "true" : "false");
    j += ",\"otaProgress\":" + String(otaProgPct);
    j += "}";
    sendJson(req, j);
}

void handleRdmJson(AsyncWebServerRequest* req) {
    String j = "{\"rdmEnabled\":" + String(cfg.artnetRdm ? "true" : "false");
    j += ",\"lineCount\":" + String(rdmLineCount());
    j += ",\"sent\":" + String(rdmSent());
    j += ",\"recv\":" + String(rdmRecv());
    j += ",\"fixturesA\":" + String(rdmCount);
    j += "}";
    sendJson(req, j);
}

void handleConfigExport(AsyncWebServerRequest* req) {
    bool includeCreds = req->hasParam("include_credentials", true) &&
                        req->getParam("include_credentials", true)->value() == "1";
    String j; cfgcore::exportJson(j, !includeCreds);
    sendJson(req, j);
}

void handleConfigImport(AsyncWebServerRequest* req) {
    if (!req->hasParam("config", true)) {
        req->send(400, "text/plain", "missing config");
        return;
    }
    String body = req->getParam("config", true)->value();
    String err;
    if (cfgcore::importJson(body, err)) {
        saveConfig();
        req->send(200, "text/plain", "Config imported. Reboot to apply.");
    } else {
        req->send(400, "text/plain", "Import failed: " + err);
    }
}

void handleHealth(AsyncWebServerRequest* req) {
    String j = "{";
    j += "\"status\":\"ok\"";
    j += ",\"uptime_s\":" + String(uptimeSec());
    j += ",\"heap_dram_free\":" + String(ESP.getFreeHeap());
    j += ",\"outputs\":[";
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (i) j += ",";
        j += "{";
        j += "\"id\":\"" + String(char('A' + i)) + "\"";
        j += ",\"universe\":" + String(cfg.outputs[i].universe);
        j += ",\"fps\":" + String(outFpsLive(i), 1);
        j += ",\"source\":\"" + String(outSrcLost[i] ? "none" : "art-net") + "\"";
        j += ",\"signal\":" + String(outSrcLost[i] ? "false" : "true");
        j += "}";
    }
    j += "]";
    j += ",\"network\":{";
    j += "\"interface\":\"" + String(g_useEth ? "eth" : (g_apMode ? "ap" : "wifi")) + "\"";
    if (g_useEth) {
        j += ",\"ip\":\"" + ETH.localIP().toString() + "\"";
        j += ",\"link_mbps\":" + String(ETH.linkSpeed());
    } else {
        j += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
        j += ",\"rssi\":" + String(WiFi.RSSI());
    }
    j += "}";
    j += ",\"alerts\":[]";
    j += "}";
    sendJson(req, j);
}

void handleConfigPost(AsyncWebServerRequest* req) {
    bool has = req->hasParam("import", true);
    if (has) {
        String body = req->arg("import");
        if (body.length() > 0) {
            String err;
            if (cfgcore::importJson(body, err)) {
                saveConfig();
                req->send(200, "text/plain", "Config imported. Reboot to apply.");
            } else {
                req->send(400, "text/plain", "Import failed: " + err);
            }
            return;
        }
    }

    bool changed = false;
    for (size_t i = 0; i < CONFIG_FIELD_COUNT; i++) {
        const CfgField& f = CONFIG_FIELDS[i];
        if (!req->hasParam(f.key, true)) continue;
        String val = req->getParam(f.key, true)->value();
        String err;
        if (cfgcore::setValue(f.key, val, err)) changed = true;
    }
    for (int o = 0; o < MAX_OUTPUTS; o++) {
        for (size_t i = 0; i < OUTPUT_FIELD_COUNT; i++) {
            const CfgOutputField& f = OUTPUT_FIELDS[i];
            String key = String(o < 2 ? char('a' + o) : 'a' + o) + "_" + f.suffix;
            if (!req->hasParam(key.c_str(), true)) continue;
            String val = req->getParam(key.c_str(), true)->value();
            String err;
            String fullKey = String(char('a' + o)) + "_" + f.suffix;
            if (cfgcore::setValue(fullKey, val, err)) changed = true;
        }
    }
    if (changed) {
        saveConfig();
        req->send(200, "text/plain", "Saved. Reboot to apply.");
    } else {
        req->send(200, "text/plain", "No changes.");
    }
}

void handleLabelsGet(AsyncWebServerRequest* req) {
    sendJson(req, "{}");
}

void handleAutoUpdatePost(AsyncWebServerRequest* req) {
    cfg.autoUpdate = !cfg.autoUpdate;
    saveConfig();
    req->send(200, "text/plain", "autoUpdate=" + String(cfg.autoUpdate));
}

void handleLabelsBody(AsyncWebServerRequest* req, uint8_t* data, size_t len,
                      size_t index, size_t total) {
    req->send(200, "text/plain", "ok");
}

void handleSetupScan(AsyncWebServerRequest* req) {
    String j = "[";
    uint8_t n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
    for (uint8_t i = 0; i < n; i++) {
        if (i) j += ",";
        j += "{\"ssid\":\"" + WiFi.SSID(i) + "\"";
        j += ",\"rssi\":" + String(WiFi.RSSI(i));
        j += ",\"enc\":" + String(WiFi.encryptionType(i));
        j += "}";
    }
    j += "]";
    sendJson(req, j);
}

void handleSetupPost(AsyncWebServerRequest* req) {
    if (req->hasParam("ssid", true) && req->hasParam("psk", true)) {
        cfg.wifiSsid = req->getParam("ssid", true)->value();
        cfg.wifiPsk  = req->getParam("psk", true)->value();
        cfg.wifiMode = NET_WIFI_STA;
        saveConfig();
        req->send(200, "text/plain", "WiFi saved. Rebooting...");
        delay(100);
        ESP.restart();
    } else {
        req->send(400, "text/plain", "missing ssid or psk");
    }
}

void handleResetPost(AsyncWebServerRequest* req) {
    if (req->hasParam("confirm", true) && req->getParam("confirm", true)->value() == "1") {
        Preferences p; p.begin("dmxgw", false);
        p.clear(); p.end();
        req->send(200, "text/plain", "Factory reset done. Rebooting...");
        delay(100);
        ESP.restart();
    } else {
        req->send(400, "text/plain", "missing confirm=1");
    }
}

void handleRebootPost(AsyncWebServerRequest* req) {
    req->send(200, "text/plain", "Rebooting...");
    delay(100);
    ESP.restart();
}

void handleOtaGithub(AsyncWebServerRequest* req) {
    if (!req->hasParam("url", true)) {
        req->send(400, "text/plain", "missing url");
        return;
    }
    String url = req->getParam("url", true)->value();
    otaFromGitHub(url);
    req->send(200, "text/plain", "OTA update started");
}

void handleOtaUrl(AsyncWebServerRequest* req) {
    if (!req->hasParam("url", true)) {
        req->send(400, "text/plain", "missing url");
        return;
    }
    String url = req->getParam("url", true)->value();
    otaFromUrl(url);
    req->send(200, "text/plain", "OTA update started");
}

void handleRdmTrigger(AsyncWebServerRequest* req) {
    if (!req->hasParam("action", true)) {
        req->send(400, "text/plain", "missing action");
        return;
    }
    String action = req->getParam("action", true)->value();
    rdm_ack_t ack;

    if (action == "discover") {
        rdmPollDirty = true;
        req->send(200, "text/plain", "RDM discovery started");
        return;
    }

    if (action == "setaddr" || action == "identify" ||
        action == "setpers" || action == "setlabel") {
        rdm_uid_t uid;
        if (!parseUidParam(req, "uid", uid)) {
            req->send(400, "text/plain", "missing or invalid uid");
            return;
        }

        if (action == "setaddr") {
            if (!req->hasParam("addr", true)) {
                req->send(400, "text/plain", "missing addr");
                return;
            }
            uint16_t addr = (uint16_t)req->getParam("addr", true)->value().toInt();
            rdmOutSelect(rdmOut);
            bool ok = rdmOpSetAddr(uid, addr, &ack);
            req->send(ok ? 200 : 500, "text/plain", ok ? "OK" : "RDM SET_ADDRESS failed");
            return;
        }

        if (action == "identify") {
            uint8_t on = 1;
            if (req->hasParam("on", true))
                on = req->getParam("on", true)->value().toInt() ? 1 : 0;
            rdmOutSelect(rdmOut);
            bool ok = rdmOpSetIdentify(uid, on != 0, &ack);
            req->send(ok ? 200 : 500, "text/plain", ok ? "OK" : "RDM IDENTIFY failed");
            return;
        }

        if (action == "setpers") {
            if (!req->hasParam("pers", true)) {
                req->send(400, "text/plain", "missing pers");
                return;
            }
            uint8_t pers = (uint8_t)req->getParam("pers", true)->value().toInt();
            rdmOutSelect(rdmOut);
            bool ok = rdmOpSetPersonality(uid, pers, &ack);
            req->send(ok ? 200 : 500, "text/plain", ok ? "OK" : "RDM SET_PERSONALITY failed");
            return;
        }

        if (action == "setlabel") {
            if (!req->hasParam("label", true)) {
                req->send(400, "text/plain", "missing label");
                return;
            }
            String label = req->getParam("label", true)->value();
            rdmOutSelect(rdmOut);
            bool ok = rdmOpSetString(uid, RDM_PID_DEVICE_LABEL, label.c_str(), &ack);
            req->send(ok ? 200 : 500, "text/plain", ok ? "OK" : "RDM SET_LABEL failed");
            return;
        }
    }

    req->send(400, "text/plain", "unknown action");
}

void handleRdmTod(AsyncWebServerRequest* req) {
    String j = "{\"count\":" + String(rdmCount) + ",\"devices\":[";
    for (int i = 0; i < rdmCount && i < RDM_TOD_MAX; i++) {
        if (i) j += ",";
        j += "{\"uid\":\"" + String(rdmTod[i].man_id, HEX) + String(rdmTod[i].dev_id, HEX) + "\"}";
    }
    j += "]}";
    sendJson(req, j);
}

bool parseUidParam(AsyncWebServerRequest* req, const char* name, rdm_uid_t& uid) {
    if (!req->hasParam(name, true)) return false;
    String hex = req->getParam(name, true)->value();
    if (hex.length() < 12) return false;
    uint32_t man = 0, dev = 0;
    sscanf(hex.substring(0, 4).c_str(), "%04x", &man);
    sscanf(hex.substring(4, 12).c_str(), "%08x", &dev);
    uid.man_id = (uint16_t)man;
    uid.dev_id = dev;
    return true;
}

void handleLedBright(AsyncWebServerRequest* req) {
    if (req->hasParam("v", true)) {
        int v = req->getParam("v", true)->value().toInt();
        setLedBrightness(constrain(v, 0, 100));
        req->send(200, "text/plain", "ok");
    } else {
        req->send(400, "text/plain", "missing v");
    }
}

String sendersJson() {
    String j = "[";
    uint32_t now = millis();
    for (int i = 0; i < MAX_SENDERS; i++) {
        const Sender& s = senders[i];
        if (s.ip == 0 || now - s.lastMs >= 5000) continue;
        if (j.length() > 1) j += ",";
        j += "{";
        j += "\"ip\":\"" + IPAddress(s.ip).toString() + "\"";
        j += ",\"proto\":" + String(s.proto);
        j += ",\"universe\":" + String(s.universe);
        j += ",\"priority\":" + String(s.priority);
        j += ",\"fps\":" + String(s.fps, 1);
        j += "}";
    }
    j += "]";
    return j;
}

String logJson() {
    String j = "[";
    for (int i = 0; i < 32; i++) {
        (void)i;
    }
    j += "]";
    return j;
}
