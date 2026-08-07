#include "led_status.h"
#include "config_schema.h"

// arduino-esp32 v3 omits LED_BUILTIN from the esp32dev variant shim.
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

// On LuxDMX hardware the LED is a single NeoPixel or a discrete RGB LED on
// the configured pins. For the v2 dev-build stub we drive the Arduino
// LEDC PWM channel so the colour functions are callable from setup/loop.

static uint8_t g_ledPin = LED_BUILTIN;
static uint8_t g_ledCh  = 0;
static bool    g_haveLed = false;
static uint32_t g_curRgb  = 0;
static bool    g_on      = false;

void initLed() {
    if (cfg.ledPin >= 0 && cfg.ledPin < 46) {
        g_ledPin = cfg.ledPin;
    }
    ledcAttachChannel(g_ledPin, 1000, 8, g_ledCh);
    g_haveLed = true;
}

void setLedColor(uint32_t rgb, bool on) {
    g_on = on;
    if (!on) {
        ledcWriteChannel(g_ledCh, 0);
        return;
    }
    g_curRgb = rgb;
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >>  8) & 0xFF;
    uint8_t b =  rgb        & 0xFF;
    // Simple average for single-channel PWM brightness.
    uint8_t v = (uint16_t(r) + g + b) / 3;
    ledcWriteChannel(g_ledCh, v);
}

void setLedColor(uint32_t rgb) { setLedColor(rgb, true); }

void setLedBrightness(uint8_t pct) {
    if (!g_haveLed) return;
    if (pct > 100) pct = 100;
    ledcWriteChannel(g_ledCh, map(pct, 0, 100, 0, 255));
}

void bootConnectingLed() {
    static uint32_t startMs = millis();
    uint32_t phase = (millis() - startMs) % 400;
    uint8_t v = (phase < 200)
        ? (uint8_t)(phase * 50 / 200)
        : (uint8_t)((400 - phase) * 50 / 200);
    ledcWriteChannel(g_ledCh, v);
}
