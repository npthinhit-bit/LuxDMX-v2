#include "web_pages.h"
#include <Arduino.h>

#include "generated/index_html.h"
#include "generated/config_html.h"
#include "generated/rdm_html.h"
#include "generated/config_saved_html.h"
#include "generated/setup_html.h"
#include "generated/setup_done_html.h"
#include "generated/reset_html.h"
#include "generated/reset_done_html.h"
#include "generated/ota_progress_html.h"
#include "generated/ota_done_html.h"
#include "generated/logo_webp.h"
#include "generated/favicon_png.h"
#include "generated/bootstrap_min_css.h"

static const char CACHE_HARD[]   = "max-age=604800";   // assets with versioned ?v= URLs
static const char CACHE_REVALID[] = "no-cache";        // pages: revalidate after every OTA

void handleLogo(AsyncWebServerRequest* req) {
    AsyncWebServerResponse* r = req->beginResponse_P(200, "image/webp", LOGO_WEBP, LOGO_WEBP_LEN);
    r->addHeader("Cache-Control", CACHE_HARD);
    req->send(r);
}

void handleFavicon(AsyncWebServerRequest* req) {
    AsyncWebServerResponse* r = req->beginResponse_P(200, "image/png", FAVICON_PNG, FAVICON_PNG_LEN);
    r->addHeader("Cache-Control", CACHE_HARD);
    req->send(r);
}

void handleBootstrapCss(AsyncWebServerRequest* req) {
    AsyncWebServerResponse* r = req->beginResponse_P(200, "text/css", BOOTSTRAP_MIN_CSS, BOOTSTRAP_MIN_CSS_LEN);
    r->addHeader("Content-Encoding", "gzip");
    r->addHeader("Cache-Control", CACHE_HARD);
    req->send(r);
}

static void sendGzipPage(AsyncWebServerRequest* req, const uint8_t* data, size_t len) {
    AsyncWebServerResponse* r = req->beginResponse_P(200, "text/html", data, len);
    r->addHeader("Content-Encoding", "gzip");
    r->addHeader("Cache-Control", CACHE_REVALID);
    req->send(r);
}

static void sendRawPage(AsyncWebServerRequest* req, const char* data) {
    AsyncWebServerResponse* r = req->beginResponse_P(200, "text/html", data);
    r->addHeader("Cache-Control", CACHE_REVALID);
    req->send(r);
}

void handleRoot(AsyncWebServerRequest* req) {
    sendGzipPage(req, INDEX_HTML, INDEX_HTML_LEN);
}

void handleConfigGet(AsyncWebServerRequest* req) {
    sendGzipPage(req, CONFIG_HTML, CONFIG_HTML_LEN);
}

void handleRdmPage(AsyncWebServerRequest* req) {
    sendGzipPage(req, RDM_HTML, RDM_HTML_LEN);
}

void handleResetGet(AsyncWebServerRequest* req) {
    sendRawPage(req, RESET_HTML);
}

void handleSetupGet(AsyncWebServerRequest* req) {
    sendRawPage(req, SETUP_HTML);
}

void handleOtaStatus(AsyncWebServerRequest* req) {
    sendRawPage(req, OTA_PROGRESS_HTML);
}
