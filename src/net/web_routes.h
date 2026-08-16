#pragma once
#include <ESPAsyncWebServer.h>
#include <Arduino.h>
#include "rdm_types.h"

// Dynamic JSON / config / OTA / RDM / LED handlers.
// These remain defined in the legacy main.cpp for now; forward declarations
// live here so web_server.cpp can wire up every route. Each handler will be
// migrated into src/net/ (web_routes.cpp) as the dynamic layer stabilizes.

// JSON snapshots
void handleDmxJson(AsyncWebServerRequest* req);
void handleSendersJson(AsyncWebServerRequest* req);
void handleLogJson(AsyncWebServerRequest* req);
void handleInfoJson(AsyncWebServerRequest* req);
void handleVersionJson(AsyncWebServerRequest* req);
void handleRdmJson(AsyncWebServerRequest* req);

// Config
void handleConfigExport(AsyncWebServerRequest* req);
void handleConfigImport(AsyncWebServerRequest* req);
void handleHealth(AsyncWebServerRequest* req);
void handleConfigPost(AsyncWebServerRequest* req);
void handleLabelsGet(AsyncWebServerRequest* req);
void handleAutoUpdatePost(AsyncWebServerRequest* req);
void handleLabelsBody(AsyncWebServerRequest* req, uint8_t* data, size_t len,
                      size_t index, size_t total);

// Setup portal
void handleSetupScan(AsyncWebServerRequest* req);
void handleSetupPost(AsyncWebServerRequest* req);

// Reset / reboot
void handleResetPost(AsyncWebServerRequest* req);
void handleRebootPost(AsyncWebServerRequest* req);

// OTA
void handleOtaGithub(AsyncWebServerRequest* req);
void handleOtaUrl(AsyncWebServerRequest* req);
void handleOtaStatusJson(AsyncWebServerRequest* req);

// RDM control
void handleRdmTrigger(AsyncWebServerRequest* req);
void handleRdmTod(AsyncWebServerRequest* req);
void handleRdmBqp(AsyncWebServerRequest* req);
void handleRdmMerge(AsyncWebServerRequest* req);

// LED brightness
void handleLedBright(AsyncWebServerRequest* req);

// Helpers (used by RDM handler)
bool parseUidParam(AsyncWebServerRequest* req, const char* name, rdm_uid_t& uid);
