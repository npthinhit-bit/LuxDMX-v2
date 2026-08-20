#include "artnet.h"
#include "logger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "netRx";

static void net_rx_task(void* arg) {
    (void)arg;
    LOG_INFO(TAG, "Network RX task started on core 0, prio 5");

    TickType_t lastWakeTime = xTaskGetTickCount();
    while (1) {
        /* Poll ArtNet packets (max 8 per 2ms tick per spec 17 §4.1) */
        if (g_artnet.ready) {
            artnet_poll();
        }

        /* 2ms tick (spec 34 §2.2, spec 14 §8.4) */
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(2));
    }
}

void net_rx_task_start(void) {
    xTaskCreatePinnedToCore(net_rx_task, "netRx", 8192, NULL, 5, NULL, 0);
}
