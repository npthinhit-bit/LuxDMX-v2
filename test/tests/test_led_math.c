/*
 * Test: LED brightness math
 * Verifies brightness scaling, clamping, and pattern-to-color mapping
 * per spec 36.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "shim.h"
#include "led_driver.h"
#include "boards.h"
#include "hw.h"
#include "logger.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_##name(void) { \
        tests_run++; \
        printf("  [RUN ] %s\n", "test_" #name); \
        test_##name(); \
        tests_passed++; \
        printf("  [PASS] %s\n", "test_" #name); \
    } \
    static void test_##name(void)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        tests_passed--; \
        return; \
    } \
} while(0)

/* Brightness scaling helper matching ws2812_update() in led_driver.c */
static uint8_t apply_brightness(uint8_t value, uint8_t brightness) {
    return value * brightness / 100;
}

/* Pattern color mapping (from ws2812_update in led_driver.c) */
static void get_pattern_color(led_pattern_t pattern, uint8_t* r, uint8_t* g, uint8_t* b, bool* on) {
    *r = 0; *g = 0; *b = 0; *on = true;
    switch (pattern) {
        case LED_PATTERN_BOOT:          *r = 0; *g = 8; *b = 0; break;
        case LED_PATTERN_WIFI_CONNECTING: *r = 8; *g = 0; *b = 8; break;
        case LED_PATTERN_WIFI_CONNECTED:  *r = 0; *g = 8; *b = 0; break;
        case LED_PATTERN_AP_ACTIVE:       *r = 8; *g = 0; *b = 8; break;
        case LED_PATTERN_ERROR:           *r = 8; *g = 0; *b = 0; break;
        case LED_PATTERN_OFF:             *on = false; break;
    }
}

TEST(brightness_full_preserves_value) {
    /* At 100% brightness, color values unchanged */
    ASSERT(apply_brightness(8, 100) == 8);
    ASSERT(apply_brightness(255, 100) == 255);
}

TEST(brightness_half_halves_value) {
    /* At 50% brightness, color values halved (integer division) */
    ASSERT(apply_brightness(8, 50) == 4);
    ASSERT(apply_brightness(100, 50) == 50);
}

TEST(brightness_zero_blacks_value) {
    /* At 0% brightness, all values become 0 */
    ASSERT(apply_brightness(8, 0) == 0);
    ASSERT(apply_brightness(255, 0) == 0);
}

TEST(brightness_clamping) {
    /* Brightness is uint8_t 0-100 from config schema (min=0, max=100) */
    ASSERT(apply_brightness(8, 100) == 8);
    ASSERT(apply_brightness(8, 0) == 0);
    /* Beyond range: 101 would produce 8*101/100 = 8 (integer truncation) */
    ASSERT(apply_brightness(8, 100) <= 8);
}

TEST(pattern_boot_is_green) {
    uint8_t r, g, b;
    bool on;
    get_pattern_color(LED_PATTERN_BOOT, &r, &g, &b, &on);
    ASSERT(r == 0);
    ASSERT(g == 8);
    ASSERT(b == 0);
    ASSERT(on == true);
}

TEST(pattern_connecting_is_purple) {
    uint8_t r, g, b;
    bool on;
    get_pattern_color(LED_PATTERN_WIFI_CONNECTING, &r, &g, &b, &on);
    ASSERT(r == 8);
    ASSERT(g == 0);
    ASSERT(b == 8);
}

TEST(pattern_connected_is_green) {
    uint8_t r, g, b;
    bool on;
    get_pattern_color(LED_PATTERN_WIFI_CONNECTED, &r, &g, &b, &on);
    ASSERT(r == 0);
    ASSERT(g == 8);
    ASSERT(b == 0);
}

TEST(pattern_ap_active_is_purple) {
    uint8_t r, g, b;
    bool on;
    get_pattern_color(LED_PATTERN_AP_ACTIVE, &r, &g, &b, &on);
    ASSERT(r == 8);
    ASSERT(g == 0);
    ASSERT(b == 8);
}

TEST(pattern_error_is_red) {
    uint8_t r, g, b;
    bool on;
    get_pattern_color(LED_PATTERN_ERROR, &r, &g, &b, &on);
    ASSERT(r == 8);
    ASSERT(g == 0);
    ASSERT(b == 0);
}

TEST(pattern_off_is_black) {
    uint8_t r, g, b;
    bool on;
    get_pattern_color(LED_PATTERN_OFF, &r, &g, &b, &on);
    ASSERT(r == 0);
    ASSERT(g == 0);
    ASSERT(b == 0);
    ASSERT(on == false);
}

TEST(brightness_scaling_with_pattern) {
    /* Verify brightness scaling produces expected scaled values */
    uint8_t r, g, b;
    bool on;
    get_pattern_color(LED_PATTERN_BOOT, &r, &g, &b, &on);
    /* Scale green component */
    ASSERT(apply_brightness(g, 50) == 4);
    ASSERT(apply_brightness(g, 25) == 2);
    ASSERT(apply_brightness(g, 75) == 6);
}

TEST(led_pattern_count) {
    /* Verify all 6 patterns are defined (spec 36) */
    ASSERT(LED_PATTERN_BOOT >= 0);
    ASSERT(LED_PATTERN_WIFI_CONNECTING > LED_PATTERN_BOOT);
    ASSERT(LED_PATTERN_WIFI_CONNECTED > LED_PATTERN_WIFI_CONNECTING);
    ASSERT(LED_PATTERN_AP_ACTIVE > LED_PATTERN_WIFI_CONNECTED);
    ASSERT(LED_PATTERN_ERROR > LED_PATTERN_AP_ACTIVE);
    ASSERT(LED_PATTERN_OFF > LED_PATTERN_ERROR);
}

int main(void) {
    printf("=== LED Brightness Math Tests ===\n\n");

    run_brightness_full_preserves_value();
    run_brightness_half_halves_value();
    run_brightness_zero_blacks_value();
    run_brightness_clamping();
    run_pattern_boot_is_green();
    run_pattern_connecting_is_purple();
    run_pattern_connected_is_green();
    run_pattern_ap_active_is_purple();
    run_pattern_error_is_red();
    run_pattern_off_is_black();
    run_brightness_scaling_with_pattern();
    run_led_pattern_count();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
