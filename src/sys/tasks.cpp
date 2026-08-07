#include "tasks.h"
#include "config_schema.h"
#include "led_status.h"
#include "display.h"
#include "firmware_version.h"
#include "sys_platform.h"
#include <Arduino.h>

TaskHandle_t g_dmxTask = nullptr;

static const uint8_t DMX_RATE_MS[] = { 25, 24, 30, 40, 50 };
static constexpr int DMX_RATE_COUNT = (int)(sizeof(DMX_RATE_MS) / sizeof(DMX_RATE_MS[0]));

uint32_t dmxPeriodMs(int out) {
    int r = cfg.outputs[out].txRate;
    if (r < 0 || r >= DMX_RATE_COUNT) r = 0;
    return DMX_RATE_MS[r];
}

uint8_t dmxRateHz(int out) {
    const uint32_t p = dmxPeriodMs(out);
    return (uint8_t)(1000u / p);
}

void dmxInitGuardBegin() {
    // Placeholder for panic-recovery guard. If init panics, the wdt resets,
    // and the device boots into the setup portal with a cleared output config.
}

void dmxInitGuardEnd() {
    // End of guarded init section.
}

void createTasks() {
    // Task implementations (dmxTxTask, netRxTask, ledTask, displayTask,
    // versionCheckTask) are declared in this header and defined in tasks.cpp.

    xTaskCreate(ledTask, "led", 2048, nullptr, 1, nullptr);
#ifdef HAS_DISPLAY
    extern bool dispReady;
    if (dispReady) xTaskCreate(displayTask, "disp", 4096, nullptr, 1, nullptr);
#endif
    xTaskCreate(versionCheckTask, "ver_chk", 12288, nullptr, 1, nullptr);

    xTaskCreatePinnedToCore(dmxTxTask, "dmxtx", 4096, nullptr, 19, &g_dmxTask, 1);
    xTaskCreatePinnedToCore(netRxTask, "netrx", 8192, nullptr, 5, nullptr, 0);
}

// ---------------------------------------------------------------------------
// Task stubs. Production implementations will be filled in as modules are
// migrated from the v1 monolith. For the v2 dev-build these are minimal
// placeholders so createTasks() links successfully.
// ---------------------------------------------------------------------------

[[noreturn]] void dmxTxTask(void* /*arg*/) {
    // Sole owner of the DMX ports. Drives RMT timing, RDM bridge.
    // v2 stub: block forever (output init is a no-op until drivers land).
    for (;;) vTaskDelay(portMAX_DELAY);
}

[[noreturn]] void netRxTask(void* /*arg*/) {
    // Core-0: drain Art-Net/sACN UDP, source-timeout re-merge, OTA.
    for (;;) {
        // artRdmPollRx();
        // readSacn();
        // artRdmDrainResponses();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

[[noreturn]] void ledTask(void* /*arg*/) {
    // Low-priority status LED driver.
    for (;;) {
        setLedColor(0, true);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

[[noreturn]] void displayTask(void* /*arg*/) {
    // Optional OLED/SPI display rendering.
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

[[noreturn]] void versionCheckTask(void* /*arg*/) {
    // Periodic firmware update check.
    for (;;) {
        versionCheck();
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
