#include "sys_platform.h"
#include <Arduino.h>

const char* PREF_NS = "dmxgw";
const char* AP_SSID = "LuxDMX-setup";

// C++-only OTA state (String is Arduino-esp32; keep it out of platform.h so the
// header stays includable from ESP-IDF C components compiled via the
// from-source arduino-esp32 build path).
String otaTarget       = "latest";
String latestVersion   = "";

#if defined(BOARD_LUXDMX_V6)
const char BOARD_ID[] = "luxdmx_v6";
#elif defined(HAS_ETH_RMII) || defined(HAS_WIRED_ETH)
const char BOARD_ID[] = "wt32eth01";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
const char BOARD_ID[] = "esp32s3-devkitc-1";
#else
const char BOARD_ID[] = "esp32-devkitc";
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S3)
const char MCU_ID[] = "esp32s3";
#else
const char MCU_ID[] = "esp32";
#endif

// Reboot / OTA scheduling state
bool     pendingWifiReset = false;
uint32_t pendingRebootAt  = 0;
volatile uint8_t otaProgPhase = 0;
volatile uint8_t otaProgPct   = 0;
bool     updateAvailable = false;
