#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Soak test monitor: logs heap stats every 60s, auto-reboot if DRAM < 30KB.
// Only compiled when LUXDMX_SOAK_TEST is defined.
void soakInit();
void soakMonitorTask(void* arg);

// Returns JSON with current soak stats for the diagnostic endpoint.
String soakStatsJson();
