#pragma once
#include <Arduino.h>

// OLED / SPI display driver shim. On boards without a display the functions
// are no-ops so initDisplay() can be called unconditionally from setup().
void initDisplay();
extern bool dispReady;
