#include "led_status.h"
#include "config_schema.h"

// arduino-esp32 v3 omits LED_BUILTIN from the esp32dev variant shim.
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

// On LuxDMX hardware the LED is a single NeoPixel or a discrete RGB LED on
// the configured pins. For the v2 dev-build stub we drive the Arduino
// LEDC PWM channel so the colour functions are callable from setup/loop.

static uint8_t g_ledPin  = LED_BUILTIN;
static uint8_t g_ledCh   = 0;
static bool    g_haveLed = false;
LedState       g_ledState;

void initLed()
{
    if (cfg.ledPin > 0 && cfg.ledPin <= 48)
    {
        g_ledPin = (uint8_t)cfg.ledPin;
    }
    g_haveLed = ledcAttachChannel(g_ledPin, 1000, 8, g_ledCh);
    if (g_haveLed)
    {
        Serial.printf("[LED] LEDC channel %u attached to pin %u\n", g_ledCh, g_ledPin);
    }
    else
    {
        Serial.printf("[LED] ledcAttachChannel failed for pin %u (channel %u) — LED disabled\n", g_ledPin, g_ledCh);
    }
    g_ledState.on = true;
}

void setLedColor(uint32_t rgb, bool on)
{
    g_ledState.rgb = rgb;
    g_ledState.on  = on;
    if (!g_haveLed)
        return;
    if (!on)
    {
        ledcWriteChannel(g_ledCh, 0);
        return;
    }
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    uint8_t v = (uint16_t(r) + g + b) / 3;
    ledcWriteChannel(g_ledCh, v);
}

void setLedColor(uint32_t rgb)
{
    setLedColor(rgb, true);
}

void setLedBrightness(uint8_t pct)
{
    if (!g_haveLed)
        return;
    if (pct > 100)
        pct = 100;
    ledcWriteChannel(g_ledCh, map(pct, 0, 100, 0, 255));
}

void bootConnectingLed()
{
    if (!g_haveLed)
        return;
    static uint32_t startMs = millis();
    uint32_t        phase   = (millis() - startMs) % 400;
    uint8_t         v       = (phase < 200) ? (uint8_t)(phase * 50 / 200) : (uint8_t)((400 - phase) * 50 / 200);
    ledcWriteChannel(g_ledCh, v);
}
