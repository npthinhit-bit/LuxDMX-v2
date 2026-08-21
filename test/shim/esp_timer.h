/*
 * Native test shim - ESP Timer
 */
#pragma once

#include <stdint.h>

/* esp_timer_get_time returns microseconds since boot (monotonic).
 * The host implementation uses clock_gettime(CLOCK_MONOTONIC). */
int64_t esp_timer_get_time(void);

/* esp_timer_get_idle_loop_time_since is not needed by tests but declared
 * for completeness; the mock returns 0. */
uint32_t esp_timer_get_idle_loop_time_since(uint64_t last);
