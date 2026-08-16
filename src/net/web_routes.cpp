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
#include "net_state.h"
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
        j += ",\"srcLost\":" + String(stats().outSrcLost[i] ? "true" : "false");
        j += ",\"txFrames\":" + String(stats().txFrames[i]);
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
        const Sender& s = senderTracker().senders[i];
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
    String j;
    cfgcore::exportJson(j, true);
    int len = j.length();
    if (len > 0 && j.charAt(len - 1) == '}') {
        j.remove(len - 1);
    }
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
    j += ",\"http_reqs\":" + String(httpReqCount);
    j += ",\"board\":\"" + String(BOARD_ID) + "\"";
    j += ",\"mcu\":\"" + String(MCU_ID) + "\"";
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
    String j = "{";
    j += "\"rdmEnabled\":" + String(cfg.artnetRdm ? "true" : "false");
    j += ",\"lineCount\":" + String(rdmLineCount());
    j += ",\"sent\":" + String(rdmSent());
    j += ",\"recv\":" + String(rdmRecv());
    j += ",\"fixturesA\":" + String(stats().rdmCount);
    j += ",\"outputs\":[";
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (i) j += ",";
        j += "{\"i\":" + String(i);
        j += ",\"uni\":" + String(cfg.outputs[i].universe);
        j += ",\"merge\":" + String(cfg.outputs[i].mergeMode);
        j += "}";
    }
    j += "]";
    j += ",\"rdmLines\":[";
    for (int i = 0; i < rdmLineCount(); i++) {
        if (i) j += ",";
        int outIdx = -1;
        for (int o = 0; o < MAX_OUTPUTS; o++) {
            if (rdmLineForOut[o] == i) { outIdx = o; break; }
        }
        j += "{\"line\":" + String(i);
        j += ",\"uni\":" + String(outIdx >= 0 ? String(cfg.outputs[outIdx].universe) : "0");
        j += "}";
    }
    j += "]";
    j += ",\"devices\":[]";
    j += ",\"available\":" + String(rdmLineCount() > 0 ? "true" : "false");
    j += ",\"scanned\":" + String(stats().rdmCount > 0 ? "true" : "false");
    j += ",\"sensorPoll\":false";
    j += ",\"bqPolicy\":" + String(artNet().bqPolicy);
    j += ",\"discStage\":0";
    j += ",\"discFound\":0";
    j += ",\"discCur\":0";
    j += ",\"discSub\":0";
    j += ",\"discovering\":false";
    j += ",\"busy\":false";
    j += ",\"artPort\":" + String(rdmLineCount() > 0 ? String(cfg.outputs[rdmOutForLine[0]].universe) : "0");
    j += ",\"artPolls\":" + String(artNet().artPolls);
    j += ",\"artTodReqs\":0";
    j += ",\"artRdmReqs\":0";
    j += ",\"artFlushes\":0";
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
    if (cfgcore::importJson(body, err) == ESP_OK) {
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
        j += ",\"source\":\"" + String(stats().outSrcLost[i] ? "none" : "art-net") + "\"";
        j += ",\"signal\":" + String(stats().outSrcLost[i] ? "false" : "true");
        j += ",\"rx_frames\":" + String(stats().rxFrameCount[i]);
        j += ",\"rx_loss\":" + String(stats().rxLossCount[i]);
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
            if (cfgcore::importJson(body, err) == ESP_OK) {
                saveConfig();
                req->send(200, "application/json", "{\"reboot\":true,\"fields\":\"config import\"}");
            } else {
                req->send(400, "application/json", "{\"reboot\":false,\"error\":\"" + err + "\"}");
            }
            return;
        }
    }

    bool changed = false;
    bool needsReboot = false;
    String rebootFields;
    for (size_t i = 0; i < CONFIG_FIELD_COUNT; i++) {
        const CfgField& f = CONFIG_FIELDS[i];
        if (!req->hasParam(f.key, true)) continue;
        String val = req->getParam(f.key, true)->value();
        String err;
        if (cfgcore::setValue(f.key, val, err) == ESP_OK) {
            changed = true;
            if (f.flags & CFG_REBOOT) {
                needsReboot = true;
                if (rebootFields.length() > 0) rebootFields += ", ";
                rebootFields += f.label;
            }
        }
    }
    bool outputChangedLive = false;
    for (int o = 0; o < MAX_OUTPUTS; o++) {
        for (size_t i = 0; i < OUTPUT_FIELD_COUNT; i++) {
            const CfgOutputField& f = OUTPUT_FIELDS[i];
            String key = String(o < 2 ? char('a' + o) : 'a' + o) + "_" + f.suffix;
            if (!req->hasParam(key.c_str(), true)) continue;
            String val = req->getParam(key.c_str(), true)->value();
            String err;
            String fullKey = String(char('a' + o)) + "_" + f.suffix;
            if (cfgcore::setValue(fullKey, val, err) == ESP_OK) {
                changed = true;
                if (f.flags & CFG_REBOOT) {
                    needsReboot = true;
                    if (rebootFields.length() > 0) rebootFields += ", ";
                    rebootFields += f.label;
                }
                if (f.flags & CFG_LIVE) outputChangedLive = true;
            }
        }
    }
    if (changed) {
        saveConfig();
        if (outputChangedLive) {
            for (int o = 0; o < MAX_OUTPUTS; o++) updateOutputRuntime(o);
        }
        String j = "{\"reboot\":" + String(needsReboot ? "true" : "false");
        if (needsReboot) {
            j += ",\"fields\":\"" + rebootFields + "\"";
        }
        j += "}";
        req->send(200, "application/json", j);
    } else {
        req->send(200, "application/json", "{\"reboot\":false}");
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
    String ver;
    if (req->hasParam("version", true)) {
        ver = req->getParam("version", true)->value();
    } else if (req->hasParam("url", true)) {
        otaTarget = req->getParam("url", true)->value();
    } else {
        req->send(400, "text/plain", "missing version or url");
        return;
    }
    if (ver.length()) {
        otaTarget = "https://github.com/thinhh0321/LuxDMX/releases/download/v" + ver + "/firmware-esp32-s3.bin";
    }
    AsyncWebServerResponse* r = req->beginResponse(302, "text/plain", "");
    r->addHeader("Location", "/ota");
    req->send(r);
    xTaskCreatePinnedToCore([](void*) {
        otaFromGitHub(otaTarget);
        vTaskDelete(NULL);
    }, "ota_gh", 8192, nullptr, 1, nullptr, 0);
}

void handleOtaUrl(AsyncWebServerRequest* req) {
    if (!req->hasParam("url", true)) {
        req->send(400, "text/plain", "missing url");
        return;
    }
    otaTarget = req->getParam("url", true)->value();
    AsyncWebServerResponse* r = req->beginResponse(302, "text/plain", "");
    r->addHeader("Location", "/ota");
    req->send(r);
    xTaskCreatePinnedToCore([](void*) {
        otaFromUrl(otaTarget);
        vTaskDelete(NULL);
    }, "ota_url", 8192, nullptr, 1, nullptr, 0);
}

void handleOtaStatusJson(AsyncWebServerRequest* req) {
    String j = "{";
    j += "\"pct\":" + String(otaProgPct);
    j += ",\"phase\":" + String(otaProgPhase);
    j += "}";
    sendJson(req, j);
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
    String j = "{\"count\":" + String(stats().rdmCount) + ",\"devices\":[";
    for (int i = 0; i < stats().rdmCount && i < RDM_TOD_MAX; i++) {
        if (i) j += ",";
        j += "{\"uid\":\"" + String(stats().rdmTod[i].man_id, HEX) + String(stats().rdmTod[i].dev_id, HEX) + "\"}";
    }
    j += "]}";
    sendJson(req, j);
}

void handleRdmBqp(AsyncWebServerRequest* req) {
    if (!req->hasParam("p", true)) {
        req->send(400, "text/plain", "missing p");
        return;
    }
    int p = req->getParam("p", true)->value().toInt();
    if (p < 0 || p > 4) {
        req->send(400, "text/plain", "p must be 0-4");
        return;
    }
    artNet().bqPolicy = (uint8_t)p;
    artNet().bqDirty = true;
    req->send(200, "text/plain", "OK");
}

void handleRdmMerge(AsyncWebServerRequest* req) {
    if (!req->hasParam("out", true) || !req->hasParam("mode", true)) {
        req->send(400, "text/plain", "missing out or mode");
        return;
    }
    int out = req->getParam("out", true)->value().toInt();
    int mode = req->getParam("mode", true)->value().toInt();
    if (out < 0 || out >= MAX_OUTPUTS || mode < 0 || mode > 4) {
        req->send(400, "text/plain", "out must be 0-3, mode 0-4");
        return;
    }
    cfg.outputs[out].mergeMode = mode;
    saveConfig();
    req->send(200, "text/plain", "OK");
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
        const Sender& s = senderTracker().senders[i];
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
