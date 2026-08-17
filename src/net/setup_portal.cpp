#include "config_schema.h"
#include "net_state.h"
#include "sys_platform.h"
#include <WiFi.h>

DNSServer dnsServer;

void startSetupPortal()
{
    g_setupPortal = true;
    bool ok       = startWiFiAP(false);
    if (ok)
    {
        dnsServer.start(53, "*", WiFi.softAPIP());
        Serial.printf("[SETUP] portal up: SSID=%s ip=%s\n", WiFi.SSID().c_str(), WiFi.softAPIP().toString().c_str());
    }
}
