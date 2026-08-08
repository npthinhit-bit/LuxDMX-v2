#pragma once
#include <stdint.h>
#include <Arduino.h>

struct LedState {
    uint32_t rgb = 0;
    bool     on  = false;
};

extern LedState g_ledState;

void initLed();
void setLedColor(uint32_t rgb, bool on);
void setLedColor(uint32_t rgb);
void setLedBrightness(uint8_t pct);
void bootConnectingLed();

