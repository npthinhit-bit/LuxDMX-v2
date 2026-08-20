#include "dmx_rmt_tx.h"
#include "esp_log.h"
#include "logger.h"
#include "driver/rmt_tx.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include <string.h>
#ifndef DMX_PACKET_SIZE
#define DMX_PACKET_SIZE 513
#endif


static const char* TAG = "dmx_rmt_tx";

dmx_output_t dmx_outputs[MAX_OUTPUTS_DRV] = {0};

/* ---- Byte LUT (lazy init, spec 14 section 3.4) ---- */
#define BYTE_LUT_MAX_SYMS 6
typedef struct {
    rmt_symbol_word_t syms[BYTE_LUT_MAX_SYMS];
    int nsym;
} byte_lut_entry_t;

static byte_lut_entry_t byte_lut[256];
static byte_lut_entry_t byte_lut_inv[256];
static bool lut_initialized = false;

static void init_lut(void) {
    if (lut_initialized) return;
    lut_initialized = true;
    for (int b = 0; b < 256; b++) {
        /* Encode a DMX byte: start bit(0) + 8 data bits(LSB first) + 2 stop bits(1)
           Each bit = 4 RMT ticks at 1MHz (4us = 250 kbaud). */
        rmt_symbol_word_t w[BYTE_LUT_MAX_SYMS] = {0};
        int levels[10] = {0};
        int durations[10] = {0};
        int d_idx = 0;
        /* Start bit: space (low, 4 ticks) */
        levels[d_idx] = 0; durations[d_idx] = 4; d_idx++;
        /* 8 data bits LSB first */
        for (int bit = 0; bit < 8; bit++) {
            int val = (b >> bit) & 1;
            int lvl = val ? 1 : 0; /* 1=mark(high), 0=space(low) */
            if (d_idx > 0 && levels[d_idx-1] == lvl) {
                durations[d_idx-1] += 4;
            } else {
                levels[d_idx] = lvl; durations[d_idx] = 4; d_idx++;
            }
        }
        /* 2 stop bits (mark, high, 8 ticks) */
        if (d_idx > 0 && levels[d_idx-1] == 1) {
            durations[d_idx-1] += 8;
        } else {
            levels[d_idx] = 1; durations[d_idx] = 8; d_idx++;
        }
        /* Pack into rmt_symbol_word_t (2 level/duration pairs per word) */
        int syms_used = 0;
        int d = 0;
        while (d < d_idx && syms_used < BYTE_LUT_MAX_SYMS) {
            w[syms_used] = (rmt_symbol_word_t){0};
            w[syms_used].level0 = levels[d]; w[syms_used].duration0 = durations[d]; d++;
            if (d < d_idx) {
                w[syms_used].level1 = levels[d]; w[syms_used].duration1 = durations[d]; d++;
            }
            syms_used++;
        }
        for (int i = 0; i < syms_used; i++) {
            byte_lut[b].syms[i] = w[i];
        }
        byte_lut[b].nsym = syms_used;
        /* Inverted: swap levels */
        for (int i = 0; i < syms_used; i++) {
            rmt_symbol_word_t inv = w[i];
            inv.level0 = !inv.level0;
            inv.level1 = !inv.level1;
            byte_lut_inv[b].syms[i] = inv;
        }
        byte_lut_inv[b].nsym = syms_used;
    }
}

bool rmtDmxInit(RmtDmx* r, int gpio, int rmtChannel) {
    if (!r) return false;
    memset(r, 0, sizeof(*r));
    r->breakTime = DMX_BREAK_US;
    r->mabTime = DMX_MAB_US;
    r->channel = rmtChannel;

    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num = (gpio_num_t)gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = DMX_RMT_CLOCK_HZ,
        .mem_block_symbols = 256,
    };
    #if SOC_RMT_SUPPORT_DMA
    tx_cfg.flags.with_dma = true;
    r->hasDma = true;
    #endif

    esp_err_t err = rmt_new_tx_channel(&tx_cfg, &r->chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel ch%d failed: %s", rmtChannel, esp_err_to_name(err));
        return false;
    }

    err = rmt_enable(r->chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_channel_enable ch%d failed: %s", rmtChannel, esp_err_to_name(err));
        return false;
    }

    rmt_copy_encoder_config_t enc_cfg = {};
    err = rmt_new_copy_encoder(&enc_cfg, &r->enc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_copy_encoder failed: %s", esp_err_to_name(err));
        return false;
    }

    r->sym = heap_caps_malloc(RMT_DMX_MAX_SYM * sizeof(rmt_symbol_word_t),
                              MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!r->sym) {
        ESP_LOGE(TAG, "heap_caps_malloc failed for symbol buffer");
        return false;
    }
    r->nsym = 0;

    LOG_INFO(TAG, "RMT DMX TX ch%d on GPIO%d initialized (DMA=%d)", rmtChannel, gpio, r->hasDma);
    return true;
}

bool rmtDmxIdle(RmtDmx* r) {
    if (!r || !r->chan) return false;
    esp_err_t err = rmt_tx_wait_all_done(r->chan, 0);
    return err == ESP_OK;
}

int rmtDmxEncode(RmtDmx* r, const uint8_t* data, int nslots) {
    if (!r || !r->sym || !data || nslots <= 0) return 0;
    if (nslots > DMX_PACKET_SIZE) nslots = DMX_PACKET_SIZE;

    init_lut();

    int wi = 0; /* symbol write index */
    int ns = r->breakTime; /* break duration in us = ticks at 1MHz */

    /* Break symbol: low for breakTime us */
    r->sym[wi++] = (rmt_symbol_word_t){
        .level0 = 0, .duration0 = (uint16_t)ns,
        .level1 = 1, .duration1 = (uint16_t)r->mabTime,
    };

    /* Encode data bytes */
    byte_lut_entry_t* lut = r->invert ? byte_lut_inv : byte_lut;
    for (int i = 0; i < nslots && wi + BYTE_LUT_MAX_SYMS <= RMT_DMX_MAX_SYM; i++) {
        byte_lut_entry_t* e = &lut[data[i]];
        for (int s = 0; s < e->nsym && wi < RMT_DMX_MAX_SYM; s++) {
            r->sym[wi++] = e->syms[s];
        }
    }

    r->nsym = wi;
    return wi;
}

void rmtDmxKick(RmtDmx* r, const uint8_t* data, int nslots) {
    if (!r || !r->chan) return;
    if (!rmtDmxIdle(r)) return; /* skip if still transmitting */
    int n = rmtDmxEncode(r, data, nslots);
    if (n <= 0) return;
    esp_err_t err = rmt_transmit(r->chan, r->enc, r->sym, n * sizeof(rmt_symbol_word_t), NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_transmit failed: %s", esp_err_to_name(err));
    }
}

void rmtDmxWait(RmtDmx* r) {
    if (!r || !r->chan) return;
    rmt_tx_wait_all_done(r->chan, 60000); /* 60ms timeout */
}