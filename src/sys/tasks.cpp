#include "tasks.h"
#include "config_schema.h"
#include "config_enums.h"
#include "led_status.h"
#include "display.h"
#include "firmware_version.h"
#include "sys_platform.h"
#include "dmx_buffer.h"
#include "dmx_rmt.h"
#include "output_init.h"
#include "rdm_engine.h"
#include "frame_router.h"
#include "merge_engine.h"
#include "scene_engine.h"
#include "stats.h"
#include "sender_tracker.h"
#include "net/artnet.h"
#include "net/sacn.h"
#include <Arduino.h>
#include <Preferences.h>

TaskHandle_t g_dmxTask = nullptr;
TickType_t xLastWakeTime = 0;

static const uint8_t DMX_RATE_MS[] = { 25, 24, 30, 40, 50 };
static const int DMX_RATE_COUNT = (int)(sizeof(DMX_RATE_MS) / sizeof(DMX_RATE_MS[0]));

uint32_t dmxPeriodMs(int out) {
    int r = cfg.outputs[out].txRate;
    if (r < 0 || r >= DMX_RATE_COUNT) r = 0;
    return DMX_RATE_MS[r];
}

uint8_t dmxRateHz(int out) {
    const uint32_t p = dmxPeriodMs(out);
    return (uint8_t)(1000u / p);
}

static uint8_t s_crashCount = 0;
static const char* PREF_NS_TASKS = "dmxgw";
static const uint32_t DMX_GUARD_TTL_MS = 3000;

void dmxInitGuardBegin() {
    Preferences p;
    p.begin(PREF_NS_TASKS, false);
    s_crashCount = p.getUChar("dmxcrash", 0);
    p.end();
    if (s_crashCount > 0) {
        Serial.printf("[DMX] init guard: crash count=%d (progressive disable)\n", s_crashCount);
        for (int i = s_crashCount - 1; i >= 0 && i < MAX_OUTPUTS; i--) {
            Serial.printf("[DMX] out%d disabled by crash guard\n", i);
            cfg.outputs[i].enabled = false;
        }
    }
}

void dmxInitGuardEnd() {
    Preferences p;
    p.begin(PREF_NS_TASKS, false);
    p.putUChar("dmxcrash", s_crashCount + 1);
    p.end();
    uint32_t t0 = millis();
    while (millis() - t0 < DMX_GUARD_TTL_MS) {
        vTaskDelay(10);
    }
    p.begin(PREF_NS_TASKS, false);
    uint8_t was = p.getUChar("dmxcrash", s_crashCount + 1);
    p.end();
    if (was == s_crashCount + 1) {
        Preferences p2;
        p2.begin(PREF_NS_TASKS, false);
        p2.putUChar("dmxcrash", 0);
        p2.end();
        Serial.println("[DMX] init guard: stable boot, crash counter reset");
    }
}

void createTasks() {
    xTaskCreate(ledTask, "led", 2048, nullptr, 1, nullptr);
    if (dispReady) xTaskCreate(displayTask, "disp", 4096, nullptr, 1, nullptr);
    xTaskCreate(versionCheckTask, "ver_chk", 12288, nullptr, 1, nullptr);

    xTaskCreatePinnedToCore(dmxTxTask, "dmxtx", 8192, nullptr, 19, &g_dmxTask, 1);
    xTaskCreatePinnedToCore(netRxTask, "netrx", 8192, nullptr, 5, nullptr, 0);
}

void updateLedFromNet() {
    extern struct LedState g_ledState;
    g_ledState.rgb = 0x000a00;
    g_ledState.on = true;
}

void renderDisplay() {
}

static void snapshotAndTransmit(int outIdx) {
    if (!outReady[outIdx]) return;
    if (cfg.outputs[outIdx].txStyle == TXSTYLE_DELTA && !stats().outSrcLost[outIdx]) return;
    uint8_t frame[DMX_PACKET_SIZE];
    if (!dmxBufSnapshot(outIdx, frame)) return;
    RmtDmx* rd = &g_outputs[outIdx].rmt;
    if (rmtDmxIdle(rd)) {
        rmtDmxKick(rd, frame, DMX_PACKET_SIZE);
        stats().txFrames[outIdx]++;
        stats().outLastDmxMs[outIdx] = millis();
        stats().outFps[outIdx] = (float)dmxRateHz(outIdx);
    }
}

static void flushArtSyncStaged() {
    if (!artNet().syncMode) return;
    uint32_t now = millis();
    if (now - artNet().syncLastMs > ARTSYNC_TIMEOUT_MS) {
        Serial.println("[ART] ArtSync timeout, falling back to immediate");
        artNet().syncMode = false;
        commitArtSyncStaged();
    }
}

static void dmxFrameTick() {
    uint32_t now = millis();
    bool anyDelta = false;

    flushArtSyncStaged();
    sceneFadeStep();

    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (cfg.outputs[i].enabled && outReady[i]) {
            if (cfg.outputs[i].txStyle == TXSTYLE_DELTA) anyDelta = true;
            if (now - stats().outLastDmxMs[i] >= dmxPeriodMs(i)) {
                snapshotAndTransmit(i);
            }
        }
    }
    if (!anyDelta) mergeOutputTimed();
}

[[noreturn]] void dmxTxTask(void* /*arg*/) {
    Serial.println("[DMX] tx task running on core 1");
    for (;;) {
        dmxFrameTick();
        vTaskDelayUntil(&xLastWakeTime, 1);
    }
}

[[noreturn]] void netRxTask(void* /*arg*/) {
    Serial.println("[NET] rx task running on core 0");
    for (;;) {
        artRdmPollRx();          // producer: recv <= 8 Art-Net packets -> ring
        artPktDispatchAll();     // consumer: dispatch enqueued Art-Net packets
        readSacn();              // producer+consumer: recv <= 4/socket -> dispatch
        artRdmDrainResponses();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

[[noreturn]] void ledTask(void* /*arg*/) {
    for (;;) {
        updateLedFromNet();
        setLedColor(g_ledState.rgb, g_ledState.on);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

[[noreturn]] void displayTask(void* /*arg*/) {
    for (;;) {
        if (dispReady) renderDisplay();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

[[noreturn]] void versionCheckTask(void* /*arg*/) {
    for (;;) {
        versionCheck();
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
