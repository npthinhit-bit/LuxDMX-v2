#include "tasks.h"
#include "config_engine.h"
#include "logger.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "dmx_buffer.h"

static const char* TAG = "lux_sys";
static const char* NVS_NS = "dmxgw";
static const char* CRASH_KEY = "dmxcrash";

/* TX rate table (spec 34 �2.1) */
static const uint32_t DMX_RATE_PERIOD_MS[] = {25, 24, 30, 40, 50};
static const float DMX_RATE_HZ[] = {40.0f, 41.6667f, 33.3333f, 25.0f, 20.0f};
static const int DMX_RATE_COUNT = 5;

/* Stability window (spec 35 �2): 3000 ms in 10 ms steps. */
static const uint32_t GUARD_TTL_MS = 3000;
static const uint32_t GUARD_WAIT_STEP_MS = 10;

uint8_t dmxInitGuardBegin(void) {
    nvs_handle_t h;
    uint8_t crashCount = 0;

    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        LOG_ERROR(TAG, "nvs_open(NVS_READONLY) failed for crash counter");
        return 0;
    }

    if (nvs_get_u8(h, CRASH_KEY, &crashCount) != ESP_OK) {
        crashCount = 0;
    }
    nvs_close(h);

    if (crashCount > 0) {
        LOG_WARN(TAG, "Crash recovery: counter=%u, disabling outputs [0..%u]",
                 crashCount, crashCount - 1);
        for (int i = (int)crashCount - 1; i >= 0; i--) {
            if (i < MAX_OUTPUTS) {
                cfg.outputs[i].en = 0;
                LOG_WARN(TAG, "Output %d disabled (crash recovery)", i);
            }
        }
    } else {
        LOG_INFO(TAG, "No crash recorded, all outputs enabled");
    }

    return crashCount;
}

void dmxInitGuardEnd(uint8_t crashCount) {
    nvs_handle_t h;
    uint8_t written = crashCount + 1;

    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        LOG_ERROR(TAG, "nvs_open(NVS_READWRITE) failed for crash counter write");
        return;
    }

    if (crashCount == 0) {
        nvs_set_u8(h, CRASH_KEY, 1);
    }
    nvs_set_u8(h, CRASH_KEY, written);
    nvs_commit(h);
    nvs_close(h);

    LOG_INFO(TAG, "Stability window: counter=%u, waiting %ums", written, GUARD_TTL_MS);

    uint32_t steps = GUARD_TTL_MS / GUARD_WAIT_STEP_MS;
    for (uint32_t i = 0; i < steps; i++) {
        vTaskDelay(pdMS_TO_TICKS(GUARD_WAIT_STEP_MS));
    }

    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        LOG_ERROR(TAG, "nvs_open(NVS_READONLY) failed for stability verification");
        return;
    }

    uint8_t verify = written;
    if (nvs_get_u8(h, CRASH_KEY, &verify) != ESP_OK) {
        verify = written;
    }
    nvs_close(h);

    if (verify == written) {
        if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_u8(h, CRASH_KEY, 0);
            nvs_commit(h);
            nvs_close(h);
            LOG_INFO(TAG, "Stable boot confirmed, crash counter reset to 0");
        }
    } else {
        LOG_WARN(TAG, "Crash during stability window (expected=%u got=%u), counter elevated",
                 written, verify);
    }
}

uint32_t dmxPeriodMs(int txRateIdx) {
    if (txRateIdx < 0 || txRateIdx >= DMX_RATE_COUNT) {
        txRateIdx = 0;
    }
    return DMX_RATE_PERIOD_MS[txRateIdx];
}

float dmxRateHz(int txRateIdx) {
    if (txRateIdx < 0 || txRateIdx >= DMX_RATE_COUNT) {
        txRateIdx = 0;
    }
    return DMX_RATE_HZ[txRateIdx];
}
