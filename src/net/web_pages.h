#pragma once
#include <ESPAsyncWebServer.h>

// Serve the static HTML pages that extra_scripts.py embeds from
// src/pages/*.html into src/generated/<page>_html.h. Each page is gzip
// (index/config/rdm) or a plain PROGMEM string literal; the handlers here
// apply the matching Content-Encoding + Cache-Control headers and delegate
// to the right PROGMEM blob. The route table lives in web_server.cpp.

// Static asset handlers
void handleLogo(AsyncWebServerRequest* req);
void handleFavicon(AsyncWebServerRequest* req);
void handleBootstrapCss(AsyncWebServerRequest* req);

// Page handlers (gzip for index/config/rdm, plain string for the rest)
void handleRoot(AsyncWebServerRequest* req);
void handleConfigGet(AsyncWebServerRequest* req);
void handleRdmPage(AsyncWebServerRequest* req);
void handleResetGet(AsyncWebServerRequest* req);
void handleSetupGet(AsyncWebServerRequest* req);
void handleOtaStatus(AsyncWebServerRequest* req);
