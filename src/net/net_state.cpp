#include "net_state.h"
#include "config_schema.h"
#include "config_core.h"
#include "led_status.h"   // setLedColor, bootConnectingLed
#include "sys_platform.h"
#include <ETH.h>
#include <Preferences.h>
#include <esp_wifi.h>

bool g_apMode = false;
bool g_useEth = false;
bool g_apWiredFallback = false;
bool g_ethFallback = false;
bool g_setupPortal = false;

bool netConnected() {
#if defined(HAS_WIRED_ETH)
    if (g_useEth) return ETH.linkUp() && ETH.localIP() != IPAddress(0,0,0,0);
#endif
    if (g_apMode) return WiFi.softAPIP() != IPAddress(0,0,0,0);
    return WiFi.status() == WL_CONNECTED;
}

IPAddress netLocalIP() {
#if defined(HAS_WIRED_ETH)
    if (g_useEth) return ETH.localIP();
#endif
    if (g_apMode) return WiFi.softAPIP();
    return WiFi.localIP();
}

IPAddress netSubnetMask() {
#if defined(HAS_WIRED_ETH)
    if (g_useEth) return ETH.subnetMask();
#endif
    if (g_apMode) return IPAddress(255, 255, 255, 0);
    return WiFi.subnetMask();
}

IPAddress netGatewayIP() {
#if defined(HAS_WIRED_ETH)
    if (g_useEth) return ETH.gatewayIP();
#endif
    if (g_apMode) return WiFi.softAPIP();
    return WiFi.gatewayIP();
}

String netSSID() {
#if defined(HAS_WIRED_ETH)
    if (g_useEth) return String("Ethernet");
#endif
    if (g_apMode) return WiFi.softAPSSID();
    return WiFi.SSID();
}

int netRSSI() {
#if defined(HAS_WIRED_ETH)
    if (g_useEth) return 0;
#endif
    if (g_apMode) return 0;
    return (int)WiFi.RSSI();
}

bool parseIp(const String& s, IPAddress& out) {
    if (s.length() == 0) { out = IPAddress(0,0,0,0); return false; }
    return out.fromString(s);
}

void applyStaStaticIp() {
    if (!cfg.staticIp) { WiFi.config((uint32_t)0, (uint32_t)0, (uint32_t)0); return; }
    IPAddress ip, gw, sn, dns;
    parseIp(cfg.ip, ip); parseIp(cfg.gateway, gw);
    parseIp(cfg.subnet, sn); parseIp(cfg.dns, dns);
    WiFi.config(ip, gw, sn, dns);
    Serial.printf("[WiFi] static IP %s\n", cfg.ip.c_str());
}

static bool migrateWifiCredsFromNvs() {
    Preferences p; p.begin("dmxgw", false);
    bool done = p.getBool("wifimig", false);
    if (!done) p.putBool("wifimig", true);
    p.end();
    if (done) return false;
    wifi_config_t wc;
    if (esp_wifi_get_config(WIFI_IF_STA, &wc) != ESP_OK) return false;
    String ssid = String((const char*)wc.sta.ssid);
    if (ssid.length() == 0) return false;
    cfg.wifiSsid = ssid;
    cfg.wifiPsk  = String((const char*)wc.sta.password);
    saveConfig();
    Serial.printf("[SETUP] migrated WiFi creds from WiFiManager NVS\n");
    return true;
}

void startWiFiStation() {
    bool forcePortal = false;
    if (digitalRead(0) == LOW) {
        Serial.print("[BOOT] button held, waiting...");
        uint32_t t = millis();
        while (digitalRead(0) == LOW && millis()-t < 3000) delay(50);
        forcePortal = (digitalRead(0) == LOW);
        Serial.println(forcePortal ? " -> setup portal" : " released");
    }
    if (forcePortal && cfg.staticIp) { cfg.staticIp = false; saveConfig(); }
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(cfg.hostname.c_str());
    if (cfg.wifiSsid.length() == 0) migrateWifiCredsFromNvs();
    if (forcePortal || cfg.wifiSsid.length() == 0) {
        Serial.println(cfg.wifiSsid.length() == 0 ? "[SETUP] no WiFi configured" : "[SETUP] BOOT held");
        startSetupPortal();
        return;
    }
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    setLedColor(0x0a0a0a, true);
    applyStaStaticIp();
    Serial.printf("[WiFi] joining '%s'\n", cfg.wifiSsid.c_str());
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPsk.c_str());
    { uint32_t t = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t < 30000) {
          bootConnectingLed();
          delay(200);
      } }
    if (WiFi.status() != WL_CONNECTED) {
        if (cfg.autoIpFallback) {
            // Assign a 169.254.x.x link-local address (RFC 3927).
            // Use a simple deterministic selection based on MAC to minimize collisions.
            uint8_t mac[6];
            WiFi.macAddress(mac);
            uint16_t lastTwo = (mac[4] << 8) | mac[5];
            uint8_t third = (lastTwo >> 8) & 0xFF;  // 169.254.<third>.<fourth>
            uint8_t fourth = lastTwo & 0xFF;        // avoid 0 and 255 boundaries
            if (third == 0) third = 1;
            if (fourth == 0) fourth = 1;
            IPAddress ip(169, 254, third, fourth);
            IPAddress gw(169, 254, third, 1);
            IPAddress sn(255, 255, 0, 0);
            WiFi.config(ip, gw, sn);
            Serial.printf("[WiFi] DHCP failed, AutoIP %s\n", ip.toString().c_str());
            delay(100);
            // Re-attempt connection after static config (forces DHCP retry)
            WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPsk.c_str());
            { uint32_t t = millis();
              while (WiFi.status() != WL_CONNECTED && millis() - t < 10000)
                  delay(200); }
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("[WiFi] DHCP recovered after AutoIP probe: %s\n",
                              WiFi.localIP().toString().c_str());
                connectStrongestAP();
                WiFi.setSleep(WIFI_PS_NONE);
                return;
            }
            // Still not connected — fall back to setup portal (AP mode) so creds can be reconfigured
            delay(100);
            // Fall through to setup portal below
        }
        Serial.println("[WiFi] could not join stored network — opening setup portal");
        startSetupPortal();
        return;
    }
    connectStrongestAP();
    WiFi.setSleep(WIFI_PS_NONE);
    Serial.printf("[WiFi] %s / %s  rssi=%d  bssid=%s\n",
        netSSID().c_str(), netLocalIP().toString().c_str(),
        (int)WiFi.RSSI(), WiFi.BSSIDstr().c_str());
}

void connectStrongestAP() {
    String ssid = cfg.wifiSsid;
    if (ssid.length() == 0) return;
    int curRssi = (int)WiFi.RSSI();
    int n = WiFi.scanNetworks(false, true);
    int bestIdx = -1, bestRssi = -999, bestCh = 0;
    uint8_t bestBssid[6] = {0};
    Serial.printf("[SCAN] %d networks, APs for '%s':\n", n, ssid.c_str());
    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) != ssid) continue;
        Serial.printf("   bssid=%s rssi=%d ch=%d\n",
            WiFi.BSSIDstr(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
        if (WiFi.RSSI(i) > bestRssi) {
            bestRssi = WiFi.RSSI(i); bestIdx = i; bestCh = WiFi.channel(i);
            memcpy(bestBssid, WiFi.BSSID(i), 6);
        }
    }
    WiFi.scanDelete();
    if (bestIdx >= 0 && bestRssi > curRssi + 6) {
        Serial.printf("[SCAN] switching to stronger AP (rssi %d -> %d)\n", curRssi, bestRssi);
        WiFi.begin(ssid.c_str(), cfg.wifiPsk.c_str(), bestCh, bestBssid, true);
        uint32_t t = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t < 12000)
            bootConnectingLed();
        Serial.printf("[SCAN] reconnected rssi=%d bssid=%s\n",
            (int)WiFi.RSSI(), WiFi.BSSIDstr().c_str());
    }
}

bool startWiFiAP(bool requirePw) {
    const char* pw = cfg.apPassword.length() >= 8 ? cfg.apPassword.c_str() : nullptr;
    if (requirePw && !pw) {
        Serial.println("[WiFi] AP fallback needs password (>=8 chars)");
        return false;
    }
    g_apMode = true; g_useEth = false;
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP(cfg.hostname.c_str(), pw);
    WiFi.setSleep(WIFI_PS_NONE);
    Serial.printf("[WiFi] AP \"%s\" %s %s  ip=%s\n",
        cfg.hostname.c_str(), ok ? "up" : "FAILED",
        pw ? "(WPA2)" : "(open)", WiFi.softAPIP().toString().c_str());
    return ok;
}
