#pragma once
#include <WiFi.h>
#include <DNSServer.h>

// Network interface accessors — abstract over WiFi STA/AP and wired Ethernet.
// Returns true if the active interface has a live link.
bool netConnected();

// Current IP / subnet / gateway for the active interface.
IPAddress netLocalIP();
IPAddress netSubnetMask();
IPAddress netGatewayIP();
String netSSID();
int netRSSI();

// Mark whether WiFi is in AP mode (standalone) or STA mode (joined a network).
// Wired Ethernet, if enabled, overrides — use g_useEth to check.
extern bool g_apMode;
extern bool g_useEth;
extern bool g_apWiredFallback;
extern bool g_ethFallback;

// Setup portal is the active "network" (no real link) on first run.
// When true, loop() pumps the captive DNS and onNotFound redirects to /.
extern bool g_setupPortal;
extern DNSServer dnsServer;

void startSetupPortal();

// Bring up WiFi in station mode (join the stored network, strongest AP).
// Falls back to setup portal if no creds or BOOT held.
void startWiFiStation();

// Bring up WiFi as a standalone access point.
// requirePw = true refuses to open an unsecured AP (wired link-loss fallback).
bool startWiFiAP(bool requirePw = false);

// Join the strongest AP for the stored SSID (mesh roaming fix).
void connectStrongestAP();

// Parse a dotted-quad IP string into an IPAddress. Returns false if empty/invalid.
bool parseIp(const String& s, IPAddress& out);
