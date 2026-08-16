#include "ota.h"
#include "rate_limiter.h"
#include "sys_platform.h"
#include "config_schema.h"
#include "config_core.h"
#include "ota_sign.h"
#include "firmware_version.h"
#include <Arduino.h>
#include <Update.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFi.h>

#ifndef OTA_SIGN_ENABLED
#define OTA_SIGN_ENABLED 0
#endif

void otaBootUpdate() {
    Preferences p;
    p.begin("dmxgw", false);
    uint8_t bootTries = p.getUChar("boottry", 0);
    p.end();

    if (bootTries >= OTA_BOOT_TRIES) {
        Serial.println("[OTA] boot fail count exceeded, factory reset");
        Preferences p2;
        p2.begin("dmxgw", false);
        p2.putUChar("boottry", 0);
        p2.end();
        return;
    }

    if (bootTries > 0) {
        Serial.printf("[OTA] boot retry %d/%d\n", bootTries, OTA_BOOT_TRIES);
        Preferences p3;
        p3.begin("dmxgw", false);
        p3.putUChar("boottry", bootTries + 1);
        p3.end();
    }
}

void otaUploadChunk(AsyncWebServerRequest* request, const String& filename,
                    size_t index, uint8_t* data, size_t len, bool final) {
    (void)filename;
    if (index == 0) {
        uint32_t ip = (uint32_t)request->client()->remoteIP();
        if (!g_otaRateLimiter.allow(ip)) {
            otaProgPhase = 3;
            request->send(429, "text/plain", "Too Many Requests");
            return;
        }
        otaProgPhase = 1;
        otaProgPct = 0;
        size_t fwSize = request->contentLength();
        if (!Update.begin(fwSize > 0 ? fwSize : UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            otaProgPhase = 3;
            Update.printError(Serial);
            request->send(500, "text/plain", "OTA begin failed");
            return;
        }
    }
    if (Update.write(data, len) != len) {
        otaProgPhase = 3;
        Update.printError(Serial);
    }
    if (Update.size() > 0) {
        otaProgPct = (uint8_t)(Update.progress() * 100 / Update.size());
    }
    if (!final) return;

#if OTA_SIGN_ENABLED
    if (!otaVerifyAndCommit()) {
        otaProgPhase = 3;
        request->send(500, "text/plain", "Signature verification failed");
        return;
    }
#endif
    if (Update.end(true)) {
        otaProgPhase = 2;
        otaProgPct = 100;
        Serial.println("[OTA] update successful, rebooting");
        request->send(200, "text/plain", "Update OK. Rebooting...");
        delay(100);
        ESP.restart();
    } else {
        otaProgPhase = 3;
        Update.printError(Serial);
        request->send(500, "text/plain", "Update failed");
    }
}

void initOTA() {
}

void otaFromGitHub(const String& url) {
    Serial.println("[OTA] GitHub update requested");
    otaProgPhase = 1;
    otaProgPct = 0;

    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode <= 0) {
        Serial.printf("[OTA] HTTP GET failed: %s\n", http.errorToString(httpCode).c_str());
        otaProgPhase = 3;
        http.end();
        return;
    }
    size_t fwSize = http.getSize();
    if (fwSize == 0) {
        otaProgPhase = 3;
        http.end();
        return;
    }
    if (!Update.begin(fwSize, U_FLASH)) {
        otaProgPhase = 3;
        Update.printError(Serial);
        http.end();
        return;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[1024];
    size_t written = 0;
    while (http.connected() && (fwSize == 0 || written < fwSize)) {
        if (stream->available()) {
            size_t toRead = min((size_t)sizeof(buf), fwSize - written);
            int r = stream->read(buf, toRead);
            if (r <= 0) break;
            if (Update.write(buf, r) != (size_t)r) {
                otaProgPhase = 3;
                Update.printError(Serial);
                break;
            }
            written += r;
            otaProgPct = (uint8_t)(written * 100 / fwSize);
        }
        delay(1);
    }
    http.end();

#if OTA_SIGN_ENABLED
    if (!otaVerifyAndCommit()) {
        otaProgPhase = 3;
        return;
    }
#endif
    if (Update.end(true)) {
        otaProgPhase = 2;
        otaProgPct = 100;
        delay(100);
        ESP.restart();
    } else {
        otaProgPhase = 3;
    }
}

void otaFromUrl(const String& url) {
    otaFromGitHub(url);
}
