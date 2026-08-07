#pragma once
#include <Arduino.h>

// otaBootUpdate: check the boot-retry partition on every power-up.
// If the last boot was an OTA that failed verification, roll back to
// the other partition or fall back to a factory reset after OTA_BOOT_TRIES.
void otaBootUpdate();

// initOTA: register HTTP OTA upload handlers on the web server.
// Must be called after webRegisterRoutes() / http.begin().
void initOTA();
