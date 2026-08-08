#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <Arduino.h>

// NVS namespace and setup-portal constants.
extern const char* PREF_NS;
extern const char* AP_SSID;

// Compile-time board identity for /config and /info.
extern const char BOARD_ID[];
extern const char MCU_ID[];

// OTA retry cap: a broken update gets this many boot attempts before giving up.
static const uint8_t OTA_BOOT_TRIES = 3;

// DMX treated as "live" this long after the last frame.
static const uint32_t DMX_LIVE_MS = 1500;

// ---------------------------------------------------------------------------
// Reboot scheduling — loop() reboots when pendingRebootAt is due and non-zero.
// Zero = no pending reboot. Set by config-save / OTA / reset / reboot handlers.
// ---------------------------------------------------------------------------
extern uint32_t pendingRebootAt;
// Clear WiFi creds before the next reboot (reset flow).
extern bool     pendingWifiReset;

// Live OTA progress, polled by the update page via /ota/status. Written from
// the HTTP OTA callbacks, read from the web handler; plain aligned bytes.
extern volatile uint8_t otaProgPhase;     // 0 idle, 1 downloading+writing, 2 finalizing, 3 error
extern volatile uint8_t otaProgPct;       // 0..100 over the streamed image
extern bool     updateAvailable;          // a newer release is waiting (checked in setup)

// Current OTA target and latest available version (from GitHub releases check).
extern String otaTarget;
extern String latestVersion;
