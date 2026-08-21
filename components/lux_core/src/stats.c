/*
 * spec 13 - Stats
 *
 * Passive telemetry accumulator. Maintains the global frame counters, per-output
 * TX/RX timestamps, source-loss flags, RDM activity counters, RDM TOD table,
 * and a rate-limited circular change log. Producers (frame router on core 0,
 * RDM task, WebSocket handler) write directly into g_stats; consumers read
 * through the stats() accessor reference.
 *
 * maybeLog() is called by the frame router for the currently-viewed output only
 * (gated by viewOutput() in output_init). This function enforces the spec 13
 * section 8 rate limit: at most one entry per output per 100 ms; sooner
 * arrivals are silently dropped.
 */
#include "stats.h"
#include "config_engine.h"
#include "logger.h"
#include "esp_timer.h"
#include <string.h>

/* Global stats singleton -- zero-initialized by BSS default (spec 13 section 6). */
StatsState g_stats;

/* Per-output rate-limit state: last log commit time in ms (spec 13 section 8). */
static uint32_t s_lastLogMs[MAX_OUTPUTS_STATS];

/* Monotonic uptime in ms -- same facility used across the codebase. */
static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void statsInit(void) {
    g_stats.bootMs = now_ms();
}

StatsState* stats(void) {
    return &g_stats;
}

/* Pack a single 13-byte log entry: 4-byte IP, 1-byte proto, 8-byte channel data. */
static void encodeEntry(uint8_t* e, uint32_t ip, uint8_t proto,
                        uint16_t channel, uint8_t value) {
    memcpy(e + 0, &ip, sizeof(ip));
    e[4] = proto;
    memcpy(e + 5, &channel, sizeof(channel));
    e[7] = value;
    memset(e + 8, 0, 5);
}

void maybeLog(int outIdx, uint32_t ip, uint8_t proto, uint16_t channel, uint8_t value) {
    if (outIdx < 0 || outIdx >= MAX_OUTPUTS_STATS) return;

    uint32_t now = now_ms();
    if ((uint32_t)(now - s_lastLogMs[outIdx]) < 100) return;
    s_lastLogMs[outIdx] = now;

    uint8_t* entry = g_stats.logBuf[g_stats.logHead];
    encodeEntry(entry, ip, proto, channel, value);

    g_stats.logHead = (g_stats.logHead + 1) % LOG_BUF_CAP;
    if (g_stats.logCount < LOG_BUF_CAP) {
        g_stats.logCount++;
    } else {
        g_stats.logTail = (g_stats.logTail + 1) % LOG_BUF_CAP;
    }
}