#pragma once
#include <ESPAsyncWebServer.h>

// Static asset handlers
void handleLogo(AsyncWebServerRequest* req);
void handleFavicon(AsyncWebServerRequest* req);
void handleBootstrapCss(AsyncWebServerRequest* req);
