#include "soak_monitor.h"
#include "stats.h"
#include "sys_platform.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void soakInit() {
#ifdef LUXDMX_SOAK_TEST
    xTaskCreate(soakMonitorTask, "soak", 4096, nullptr, 1, nullptr);
#endif
}

void soakMonitorTask(void*) {
    for (;;) {
        uint32_t now = millis();
        uint32_t dramFree = ESP.getFreeHeap();

        size_t psramFree = 0;
        size_t psramTotal = 0;
#ifdef CONFIG_SPIRAM_SUPPORT
        psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
#endif

        size_t dramFreeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

        Serial.printf("[SOAK] uptime=%lu dram_free=%u dram_block=%u psram_free=%u/%u\n",
                      now / 1000,
                      (unsigned)dramFree,
                      (unsigned)dramFreeBlock,
                      (unsigned)psramFree,
                      (unsigned)psramTotal);

        if (dramFree < 30 * 1024) {
            Serial.println("[SOAK] DRAM < 30KB, rebooting");
            ESP.restart();
        }

        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

String soakStatsJson() {
    String j = "{";
    j += "\"uptime_s\":" + String(uptimeSec());
    j += ",\"dram_free\":" + String(ESP.getFreeHeap());
    j += ",\"dram_largest_block\":" + String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
#ifdef CONFIG_SPIRAM_SUPPORT
    j += ",\"psram_free\":" + String(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    j += ",\"psram_total\":" + String(heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
#else
    j += ",\"psram_free\":0";
    j += ",\"psram_total\":0";
#endif
    j += "}";
    return j;
}
