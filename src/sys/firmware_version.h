#pragma once
#include <Arduino.h>

// Firmware version: matches the git tag at release time; dev builds use
// "x.x.x-dev". Updated by the release script (scripts/set_version.sh).
extern const char FIRMWARE_VERSION[];
extern const char FIRMWARE_BUILD[];
extern const char FIRMWARE_VARIANT[];

// Check GitHub for a newer release. Updates latestVersion / otaTarget
// (globals in sys_platform.h) so the web UI can show an update banner.
void versionCheck();
