#include "web_routes.h"

void handleDmxJson(AsyncWebServerRequest* req) { req->send(501, "text/plain", "not implemented"); }
void handleSendersJson(AsyncWebServerRequest* req) { req->send(501, "text/plain", "not implemented"); }
void handleLogJson(AsyncWebServerRequest* req) { req->send(501, "text/plain", "not implemented"); }
void handleInfoJson(AsyncWebServerRequest* req) { req->send(501, "text/plain", "not implemented"); }
void handleVersionJson(AsyncWebServerRequest* req) { req->send(501, "text/plain", "not implemented"); }
void handleRdmJson(AsyncWebServerRequest* req) { req->send(501, "text/plain", "not implemented"); }
void handleConfigPost(AsyncWebServerRequest* req) { req->send(501, "text/plain", "not implemented"); }
void handleLabelsGet(AsyncWebServerRequest* req) { req->send(501, "text/plain", "not implemented"); }
void handleAutoUpdatePost(AsyncWebServerRequest* req) { req->send(501, "text/plain", "not implemented"); }
void handleLabelsBody(AsyncWebServerRequest*, uint8_t*, size_t, size_t, size_t) {}
void handleSetupScan(AsyncWebServerRequest* req) { req->send(501, "text/plain", "not implemented"); }
void handleSetupPost(AsyncWebServerRequest* req) { req->send(501, "text/plain", "not implemented"); }
void handleResetPost(AsyncWebServerRequest* req) { req->send(501, "text/plain", "not implemented"); }
void handleRebootPost(AsyncWebServerRequest* req) { req->send(501, "text/plain", "not implemented"); }
void handleOtaGithub(AsyncWebServerRequest* req) { req->send(501, "text/plain", "not implemented"); }
void handleOtaUrl(AsyncWebServerRequest* req) { req->send(501, "text/plain", "not implemented"); }
void handleOtaUploadDone(AsyncWebServerRequest* req) { req->send(501, "text/plain", "not implemented"); }
void handleOtaUploadChunk(AsyncWebServerRequest*, const String& /*filename*/, size_t, uint8_t*, size_t, bool) {}
void handleRdmTrigger(AsyncWebServerRequest* req) { req->send(501, "text/plain", "not implemented"); }
void handleLedBright(AsyncWebServerRequest* req) { req->send(501, "text/plain", "not implemented"); }
