#pragma once
#include <Arduino.h>

void otaBootUpdate();
void initOTA();

// GitHub OTA: fetch a release asset by URL.
void otaFromGitHub(const String& url);

// URL OTA: fetch firmware from an arbitrary HTTP URL.
void otaFromUrl(const String& url);
