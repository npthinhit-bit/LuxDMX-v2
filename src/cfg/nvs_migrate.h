#pragma once
#include <Arduino.h>

// NVS key migration: old 2-universe build stored per-output keys as "o0_*"/"o1_*".
// The 4-universe build uses letter prefixes "a_*" through "d_*". This helper
// performs a one-shot migration pass for outputs 0 and 1 so an upgraded device
// keeps its config without re-joining WiFi or resetting the DMX ports.
//
// migrateNvsKeys() reads each old o<n>_<suffix> key, writes the new a/b_<suffix>
// equivalent, and erases the old key. Returns the count of keys migrated.
// Safe to call multiple times (no-op after the first pass).
namespace nvs_migrate
{

int migrateNvsKeys(const char* ns);

}  // namespace nvs_migrate
