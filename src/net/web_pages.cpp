#include "web_pages.h"
#include <Arduino.h>

#include "generated/bootstrap_min_css.h"
#include "generated/favicon_png.h"
#include "generated/logo_webp.h"

static const char CACHE_HARD[] = "max-age=604800";  // assets with versioned ?v= URLs

void handleLogo(AsyncWebServerRequest* req)
{
    AsyncWebServerResponse* r = req->beginResponse_P(200, "image/webp", LOGO_WEBP, LOGO_WEBP_LEN);
    r->addHeader("Cache-Control", CACHE_HARD);
    req->send(r);
}

void handleFavicon(AsyncWebServerRequest* req)
{
    AsyncWebServerResponse* r = req->beginResponse_P(200, "image/png", FAVICON_PNG, FAVICON_PNG_LEN);
    r->addHeader("Cache-Control", CACHE_HARD);
    req->send(r);
}

void handleBootstrapCss(AsyncWebServerRequest* req)
{
    AsyncWebServerResponse* r = req->beginResponse_P(200, "text/css", BOOTSTRAP_MIN_CSS, BOOTSTRAP_MIN_CSS_LEN);
    r->addHeader("Content-Encoding", "gzip");
    r->addHeader("Cache-Control", CACHE_HARD);
    req->send(r);
}
