#include "firmware_version.h"
#include "sys_platform.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

const char FIRMWARE_VERSION[] = "0.0.0-dev";
const char FIRMWARE_BUILD[]   = __DATE__ " " __TIME__;
const char FIRMWARE_VARIANT[] = "luxdmx_4uni";

// GitHub API: latest release for this repo.
#if defined(GITHUB_REPO)
static const char* GH_RELEASES_URL = "https://api.github.com/repos/" GITHUB_REPO "/releases/latest";
#else
static const char* GH_RELEASES_URL = "https://api.github.com/repos/thinhh0321/LuxDMX/releases/latest";
#endif

void versionCheck()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    // HTTPS (WiFiClientSecure / NetworkClientSecure) allocates a large mbedTLS
    // SSL context from internal DRAM (~20 KB+). On the ESP32-S3 with PSRAM,
    // internal DRAM can be fragmented after extended runtime, causing
    // MBEDTLS_ERR_SSL_ALLOC_FAILED (-32512 in start_ssl_client). Guard the
    // attempt — skip rather than flood the serial log with SSL alloc errors
    // every 60 s. Mirrors the heap gate already used in wsPushMeta().
    if (ESP.getFreeHeap() < 60000 || ESP.getMaxAllocHeap() < 20000)
    {
        Serial.printf("[VER] skip version check: free_heap=%u max_alloc=%u\n", (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getMaxAllocHeap());
        return;
    }

    HTTPClient http;
    http.setTimeout(8000);
    http.begin(GH_RELEASES_URL);
    http.addHeader("Accept", "application/vnd.github+json");
    int code = http.GET();
    if (code != 200)
    {
        Serial.printf("[VER] version check HTTP %d\n", code);
        http.end();
        return;
    }

    size_t len = http.getSize();
    if (len == 0 || len > 65536)
    {
        http.end();
        return;
    }
    // The GitHub releases/latest response body is typically 600-900 bytes.
    // Read it in 1024-byte chunks; the tag_name field appears early.
    uint8_t     buf[1024];
    WiFiClient* stream     = http.getStreamPtr();
    bool        found      = false;
    char        latest[32] = {0};
    size_t      k          = 0;

    while (http.connected() && !found)
    {
        if (stream->available())
        {
            size_t chunk = stream->read(buf, sizeof(buf));
            if (chunk <= 0)
                break;
            for (size_t i = 0; i + 10 <= chunk; i++)
            {
                if (memcmp(buf + i, "\"tag_name\"", 10) == 0)
                {
                    // Find the value after the colon.
                    size_t v = i + 10;
                    while (v < chunk && (buf[v] == ' ' || buf[v] == ':'))
                        v++;
                    // Skip opening quote.
                    if (v < chunk && buf[v] == '"')
                        v++;
                    k = 0;
                    while (v < chunk && buf[v] != '"' && k < sizeof(latest) - 1)
                    {
                        if (buf[v] == 'v' && k == 0)
                        {
                            v++;
                            continue;
                        }  // strip leading 'v'
                        latest[k++] = (char)buf[v++];
                    }
                    latest[k] = 0;
                    if (k > 0)
                        found = true;
                    break;
                }
            }
        }
        delay(1);
    }
    http.end();
    if (!found || k == 0)
        return;

    latestVersion = String(latest);
    if (String(latest) != String(FIRMWARE_VERSION))
        updateAvailable = true;
}
