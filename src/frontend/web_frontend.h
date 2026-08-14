#pragma once
#include <ESPAsyncWebServer.h>

void handleRoot(AsyncWebServerRequest* req);
void handleConfigGet(AsyncWebServerRequest* req);
void handleRdmPage(AsyncWebServerRequest* req);
void handleSetupGet(AsyncWebServerRequest* req);
void handleResetGet(AsyncWebServerRequest* req);
void handleOtaStatus(AsyncWebServerRequest* req);
void handleConfigSaved(AsyncWebServerRequest* req);
void handleSetupDone(AsyncWebServerRequest* req);
void handleResetDone(AsyncWebServerRequest* req);
