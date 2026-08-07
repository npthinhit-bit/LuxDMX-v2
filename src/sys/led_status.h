#pragma once
#include <stdint.h>
#include <Arduino.h>

// PWM LED channel (0-15) and GPIO pin assignments for status indicators.
void initLed();
void setLedColor(uint32_t rgb, bool on);
void setLedColor(uint32_t rgb);
void setLedBrightness(uint8_t pct);  // 0-100

void bootConnectingLed();
