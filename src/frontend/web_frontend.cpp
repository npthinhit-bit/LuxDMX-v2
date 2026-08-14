#include "web_frontend.h"
#include "web_server.h"
#include "base/styles.h"
#include "base/navbar.h"
#include "base/footer.h"
#include "scripts/shared_js.h"
#include "pages/index_page.h"
#include "pages/index_css.h"
#include "scripts/index_js.h"
#include "pages/config_page.h"
#include "pages/config_css.h"
#include "scripts/config_js.h"
#include "pages/rdm_page.h"
#include "pages/rdm_css.h"
#include "scripts/rdm_js.h"
#include "pages/setup_page.h"
#include "pages/reset_page.h"
#include "pages/ota_progress_page.h"
#include "pages/ota_done_page.h"
#include "pages/config_saved_page.h"
#include "pages/setup_done_page.h"
#include "pages/reset_done_page.h"
#include <Arduino.h>

static void sendAppPage(AsyncWebServerRequest* req, const __FlashStringHelper* pageBody, const __FlashStringHelper* pageCss, const __FlashStringHelper* pageJs) {
    String html;
    html.reserve(20000);
    html += F("<!DOCTYPE html><html lang=\"en\" data-bs-theme=\"dark\"><head>");
    html += F("<meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
    html += F("<title>LuxDMX</title>");
    html += F("<link rel=\"icon\" type=\"image/png\" href=\"/favicon.png?v=__FWVER__\">");
    html += F("<link rel=\"stylesheet\" href=\"/bootstrap.min.css?v=__FWVER__\">");
    html += F("<style>");
    html += FPSTR(FRONTEND_STYLES);
    html += FPSTR(NAVBAR_CSS);
    if (pageCss) html += String(pageCss);
    html += F("</style></head><body>");
    html += FPSTR(NAVBAR_HTML);
    html += F("<script>");
    html += FPSTR(NAVBAR_JS);
    html += F("</script>");
    html += String(pageBody);
    html += FPSTR(FOOTER_HTML);
    html += FPSTR(APP_MODAL_HTML);
    html += F("<script>");
    html += FPSTR(SHARED_JS);
    html += F("</script>");
    if (pageJs) { html += F("<script>"); html += String(pageJs); html += F("</script>"); }
    html += F("</body></html>");
    AsyncWebServerResponse* r = req->beginResponse(200, "text/html", html);
    r->addHeader("Cache-Control", "no-cache");
    req->send(r);
}

static void sendRawPage(AsyncWebServerRequest* req, const __FlashStringHelper* pageHtml) {
    String html = String(pageHtml);
    AsyncWebServerResponse* r = req->beginResponse(200, "text/html", html);
    r->addHeader("Cache-Control", "no-cache");
    req->send(r);
}

void handleRoot(AsyncWebServerRequest* req) {
    sendAppPage(req, FPSTR(INDEX_PAGE_BODY), FPSTR(INDEX_PAGE_CSS), FPSTR(INDEX_PAGE_JS));
}

void handleConfigGet(AsyncWebServerRequest* req) {
    sendAppPage(req, FPSTR(CONFIG_PAGE_BODY), FPSTR(CONFIG_PAGE_CSS), FPSTR(CONFIG_PAGE_JS));
}

void handleRdmPage(AsyncWebServerRequest* req) {
    sendAppPage(req, FPSTR(RDM_PAGE_BODY), FPSTR(RDM_PAGE_CSS), FPSTR(RDM_PAGE_JS));
}

void handleSetupGet(AsyncWebServerRequest* req) {
    sendRawPage(req, FPSTR(SETUP_PAGE));
}

void handleResetGet(AsyncWebServerRequest* req) {
    sendRawPage(req, FPSTR(RESET_PAGE));
}

void handleOtaStatus(AsyncWebServerRequest* req) {
    sendRawPage(req, FPSTR(OTA_PROGRESS_PAGE));
}

void handleConfigSaved(AsyncWebServerRequest* req) {
    sendRawPage(req, FPSTR(CONFIG_SAVED_PAGE));
}

void handleSetupDone(AsyncWebServerRequest* req) {
    sendRawPage(req, FPSTR(SETUP_DONE_PAGE));
}

void handleResetDone(AsyncWebServerRequest* req) {
    sendRawPage(req, FPSTR(RESET_DONE_PAGE));
}
