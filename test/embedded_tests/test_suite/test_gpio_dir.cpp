// Unity tests for gpio_dir.h — gpioDeSet() and gpioDeInit() on real GPIO.
// Runs on real ESP32-S3 hardware (GPIO readback) and Wokwi simulation.
#include "gpio_dir.h"
#include <unity.h>

// Use GPIO2 — available on ESP32-S3 DevKitC-1, not used by default config.
#define TEST_GPIO_PIN 2

void test_gpio_set_high(void)
{
    gpioDeInit(TEST_GPIO_PIN);
    gpioDeSet(TEST_GPIO_PIN, 1);
    TEST_ASSERT_EQUAL_INT(1, gpio_get_level((gpio_num_t)TEST_GPIO_PIN));
}

void test_gpio_set_low(void)
{
    gpioDeInit(TEST_GPIO_PIN);
    gpioDeSet(TEST_GPIO_PIN, 0);
    TEST_ASSERT_EQUAL_INT(0, gpio_get_level((gpio_num_t)TEST_GPIO_PIN));
}

void test_gpio_init_output(void)
{
    gpioDeInit(TEST_GPIO_PIN);
    int level = gpio_get_level((gpio_num_t)TEST_GPIO_PIN);
    TEST_ASSERT_EQUAL_INT(1, level);
}
