#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Crash guard (spec 35) */
/* Returns the crash counter read at begin (0 = clean boot). */
uint8_t dmxInitGuardBegin(void);
/* Writes counter+1, waits 3s stability window, resets to 0 on stable boot. */
void dmxInitGuardEnd(uint8_t crashCount);

/* TX rate table (spec 34 §2.1) */
uint32_t dmxPeriodMs(int txRateIdx);
float dmxRateHz(int txRateIdx);

#ifdef __cplusplus
}
#endif
