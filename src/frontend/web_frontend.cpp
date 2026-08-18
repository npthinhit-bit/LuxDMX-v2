#include "web_frontend.h"
#include "base/footer.h"
#include "base/navbar.h"
#include "base/styles.h"
#include "firmware_version.h"
#include "pages/config_css.h"
#include "pages/config_page.h"
#include "pages/config_saved_page.h"
#include "pages/index_css.h"
#include "pages/index_page.h"
#include "pages/ota_done_page.h"
#include "pages/ota_progress_page.h"
#include "pages/rdm_css.h"
#include "pages/rdm_page.h"
#include "pages/reset_done_page.h"
#include "pages/reset_page.h"
#include "pages/setup_done_page.h"
#include "pages/setup_page.h"
#include "scripts/config_js.h"
#include "scripts/index_js.h"
#include "scripts/rdm_js.h"
#include "scripts/shared_js.h"
#include "web_server.h"
#include <Arduino.h>
#include <cstring>
#include <memory>

static void sendAppPage(AsyncWebServerRequest* req, const __FlashStringHelper* pageBody,
                        const __FlashStringHelper* pageCss, const __FlashStringHelper* pageJs)
{
    auto sp = std::make_shared<String>();
    String& html = *sp;
    html.reserve(20000);
    html += F("<!DOCTYPE html><html lang=\"en\" data-bs-theme=\"dark\"><head>");
    html += F("<meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
    html += F("<title>LuxDMX</title>");
    html += F("<link rel=\"icon\" type=\"image/png\" href=\"/favicon.png?v=__FWVER__\">");
    html += F("<link rel=\"stylesheet\" href=\"/bootstrap.min.css?v=__FWVER__\">");
    html += F("<style>");
    html += FPSTR(FRONTEND_STYLES);
    html += FPSTR(NAVBAR_CSS);
    if (pageCss)
        html += String(pageCss);
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
    if (pageJs)
    {
        html += F("<script>");
        html += String(pageJs);
        html += F("</script>");
    }
    html += F("</body></html>");
    html.replace("__FWVER__", String(FIRMWARE_VERSION));
    const size_t htmlLen = html.length();
    AsyncWebServerResponse* r = req->beginResponse("text/html", htmlLen,
        [sp](uint8_t* data, size_t maxlen, size_t index) -> size_t {
            if (index >= sp->length()) return 0;
            const size_t avail = sp->length() - index;
            const size_t n = (maxlen < avail) ? maxlen : avail;
            memcpy(data, sp->c_str() + index, n);
            return n;
        });
    r->addHeader("Content-Encoding", "gzip");
    r->addHeader("Cache-Control", "no-cache");
    req->send(r);
}

static void sendRawPage(AsyncWebServerRequest* req, const __FlashStringHelper* pageHtml)
{
    auto sp = std::make_shared<String>();
    String& html = *sp;
    html = String(pageHtml);
    html.replace("__FWVER__", String(FIRMWARE_VERSION));
    const size_t htmlLen = html.length();
    AsyncWebServerResponse* r = req->beginResponse("text/html", htmlLen,
        [sp](uint8_t* data, size_t maxlen, size_t index) -> size_t {
            if (index >= sp->length()) return 0;
            const size_t avail = sp->length() - index;
            const size_t n = (maxlen < avail) ? maxlen : avail;
            memcpy(data, sp->c_str() + index, n);
            return n;
        });
    r->addHeader("Cache-Control", "no-cache");
    req->send(r);
}

void handleRoot(AsyncWebServerRequest* req)
{
    sendAppPage(req, FPSTR(INDEX_PAGE_BODY), FPSTR(INDEX_PAGE_CSS), FPSTR(INDEX_PAGE_JS));
}

void handleConfigGet(AsyncWebServerRequest* req)
{
    sendAppPage(req, FPSTR(CONFIG_PAGE_BODY), FPSTR(CONFIG_PAGE_CSS), FPSTR(CONFIG_PAGE_JS));
}

void handleRdmPage(AsyncWebServerRequest* req)
{
    sendAppPage(req, FPSTR(RDM_PAGE_BODY), FPSTR(RDM_PAGE_CSS), FPSTR(RDM_PAGE_JS));
}

void handleSetupGet(AsyncWebServerRequest* req)
{
    sendRawPage(req, FPSTR(SETUP_PAGE));
}

void handleResetGet(AsyncWebServerRequest* req)
{
    sendRawPage(req, FPSTR(RESET_PAGE));
}

void handleOtaStatus(AsyncWebServerRequest* req)
{
    sendRawPage(req, FPSTR(OTA_PROGRESS_PAGE));
}

void handleConfigSaved(AsyncWebServerRequest* req)
{
    sendRawPage(req, FPSTR(CONFIG_SAVED_PAGE));
}

void handleSetupDone(AsyncWebServerRequest* req)
{
    sendRawPage(req, FPSTR(SETUP_DONE_PAGE));
}

void handleResetDone(AsyncWebServerRequest* req)
{
    sendRawPage(req, FPSTR(RESET_DONE_PAGE));
}
