#include "web_server.h"
#include "web_pages.h"
#include "web_routes.h"
#include "config_schema.h"
#include "sys/soak_monitor.h"
#include <Arduino.h>

AsyncWebServer http(80);
volatile uint32_t httpReqCount = 0;

// Route registration. Static page handlers live in web_pages.cpp; the dynamic
// JSON/config/OTA handlers remain in main.cpp for now (migrated to dedicated
// modules in a follow-up). This function centralizes the route table.
void webRegisterRoutes(AsyncWebServer& http) {
    http.on("/logo.webp",         HTTP_GET,  handleLogo);
    http.on("/favicon.png",       HTTP_GET,  handleFavicon);
    http.on("/favicon.ico",       HTTP_GET,  handleFavicon);
    http.on("/bootstrap.min.css", HTTP_GET,  handleBootstrapCss);
    http.on("/",                  HTTP_GET,  handleRoot);
    http.on("/dmx.json",          HTTP_GET,  handleDmxJson);
    http.on("/senders.json",      HTTP_GET,  handleSendersJson);
    http.on("/log.json",          HTTP_GET,  handleLogJson);
    http.on("/config",            HTTP_GET,  handleConfigGet);
    http.on("/config",            HTTP_POST, handleConfigPost);
    http.on("/config/export",     HTTP_GET,  handleConfigExport);
    http.on("/config/import",     HTTP_POST, handleConfigImport);
    http.on("/health",            HTTP_GET,  handleHealth);
    http.on("/diag/soak-stats",   HTTP_GET,  [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", soakStatsJson());
    });
    http.on("/setup/scan",        HTTP_GET,  handleSetupScan);
    http.on("/setup",             HTTP_GET,  handleSetupGet);
    http.on("/setup",             HTTP_POST, handleSetupPost);
    http.on("/reset",             HTTP_GET,  handleResetGet);
    http.on("/reset",             HTTP_POST, handleResetPost);
    http.on("/reboot",            HTTP_POST, handleRebootPost);
    http.on("/ota/github",        HTTP_POST, handleOtaGithub);
    http.on("/ota/url",           HTTP_POST, handleOtaUrl);
    http.on("/ota/status",        HTTP_GET,  handleOtaStatus);
    http.on("/ota/upload",        HTTP_POST, NULL, otaUploadChunk);
    http.on("/version.json",      HTTP_GET,  handleVersionJson);
    http.on("/info.json",         HTTP_GET,  handleInfoJson);
    http.on("/rdm.json",          HTTP_GET,  handleRdmJson);
    http.on("/rdm/discover",      HTTP_GET,  handleRdmTrigger);
    http.on("/rdm/setaddr",       HTTP_GET,  handleRdmTrigger);
    http.on("/rdm/identify",      HTTP_GET,  handleRdmTrigger);
    http.on("/rdm/setpers",       HTTP_GET,  handleRdmTrigger);
    http.on("/rdm/setlabel",      HTTP_GET,  handleRdmTrigger);
    http.on("/rdm/tod",           HTTP_GET,  handleRdmTod);
    http.on("/led/bright",        HTTP_GET,  handleLedBright);
    http.on("/rdm",               HTTP_GET,  handleRdmPage);
    http.on("/labels.json",       HTTP_GET,  handleLabelsGet);
    http.on("/labels",            HTTP_POST, [](AsyncWebServerRequest*){}, NULL, handleLabelsBody);
    http.on("/autoupdate",        HTTP_POST, handleAutoUpdatePost);
    http.onNotFound([](AsyncWebServerRequest* req) {
        extern bool g_setupPortal;
        if (g_setupPortal) {
            AsyncWebServerResponse* r = req->beginResponse(302, "text/plain", "");
            r->addHeader("Location", "/");
            req->send(r);
            return;
        }
        req->send(404, "text/plain", "Not found");
    });
}

void webRegisterRoutes() {
    webRegisterRoutes(http);
}
