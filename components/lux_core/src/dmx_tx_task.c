#include "dmx_buffer.h"
#include "dmx_rmt_tx.h"
#include "config_engine.h"
#include "logger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "dmx_tx";

/* Frame buffer for snapshot (513 bytes, spec 03 §9.3) */
static uint8_t tx_frame[DMX_PACKET_SIZE];

static void dmx_tx_task(void* arg) {
    (void)arg;

    /* Prime with zero frame */
    memset(tx_frame, 0, sizeof(tx_frame));
    for (int i = 0; i < MAX_OUTPUTS && i < MAX_OUTPUTS_DRV; i++) {
        dmx_output_t* out = &dmx_outputs[i];
        if (out->ready) {
            rmtDmxKick(&out->rmt, tx_frame, DMX_PACKET_SIZE);
        }
    }

    while (1) {
        for (int i = 0; i < MAX_OUTPUTS && i < MAX_OUTPUTS_DRV; i++) {
            dmx_output_t* out = &dmx_outputs[i];
            if (!out->ready) continue;

            /* Snapshot the live buffer via seqlock (spec 03 §4.4) */
            bool ok = dmxBufSnapshot(i, tx_frame);
            if (!ok) {
                /* Hold previous frame — skip this tick (spec 03 §4.5) */
                continue;
            }

            /* Non-blocking idle check (spec 14 §4.3) */
            if (!rmtDmxIdle(&out->rmt)) continue;

            /* Transmit */
            rmtDmxKick(&out->rmt, tx_frame, DMX_PACKET_SIZE);
        }

        /* 1ms tick (spec 03 §8.3, spec 14 §8.4) */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void dmx_tx_task_start(void) {
    /* Pin to core 1, priority 19 per spec 34 / 14 §10 */
    xTaskCreatePinnedToCore(dmx_tx_task, "dmxTx", 8192, NULL, 19, NULL, 1);
    LOG_INFO(TAG, "DMX transmit task started on core 1, prio 19");
}
