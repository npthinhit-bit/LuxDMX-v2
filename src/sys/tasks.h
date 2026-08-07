#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// DMX transmit task: core 1, priority 19. Sole owner of the DMX ports.
// Also services RDM (queued requests). 1 ms tick cadence.
[[noreturn]] void dmxTxTask(void*);
extern TaskHandle_t g_dmxTask;

// Network receive task: core 0, priority 5. Drains Art-Net/sACN UDP sockets,
// runs source-timeout re-merge, handles OTA.
[[noreturn]] void netRxTask(void*);

// LED status task: low priority, drives the status panel / NeoPixel.
[[noreturn]] void ledTask(void*);

// Display rendering task: low priority, draws the optional OLED/SPI display.
[[noreturn]] void displayTask(void*);

// Version check: periodic check for firmware updates over HTTPS.
[[noreturn]] void versionCheckTask(void*);

// DMX transmit timing constants.
static constexpr uint32_t DMX_MIN_PERIOD_MS = 24;
static constexpr uint32_t DELTA_FALLBACK_MS  = 800;

// Output TX rate selection.
extern uint32_t dmxPeriodMs(int out);
extern uint8_t  dmxRateHz(int out);

// Spawn all tasks. Called from setup() after all modules are initialized.
void createTasks();

void dmxInitGuardBegin();
void dmxInitGuardEnd();
