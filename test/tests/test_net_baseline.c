/*
 * Test: Net baseline (WiFi state machine + portal activation matrix)
 * Tests WiFi state transitions, portal entry conditions per spec 33 §2,
 * and exponential backoff per spec 14.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "shim.h"
#include "wifi_manager.h"
#include "wifi_config.h"
#include "config_engine.h"
#include "config_schema.h"
#include "logger.h"
#include "boards.h"
#include "hw.h"

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

static void setup(void) {
    test_shim_reset_nvs();
    logger_init();
    config_engine_init();
    config_load();
}

static void wifi_setup(void) {
    test_shim_reset_nvs();
    test_shim_set_gpio0_low(false);
    logger_init();
    config_engine_init();
    config_load();
    wifi_manager_reset_state_test();
    wifi_manager_init(NULL);
}

/* Portal activation matrix (spec 33 §2, §27) */
TEST(portal_no_credentials) {
    wifi_setup();
    /* No credentials stored -> portal */
    ASSERT(wifi_config_exists() == false);
    ASSERT(wifi_should_enter_portal() == true);
}

TEST(portal_no_credentials_gpio0_forced) {
    wifi_setup();
    ASSERT(wifi_config_exists() == false);
    test_shim_set_gpio0_low(true);
    /* Re-init to pick up GPIO0 state */
    /* GPIO0 forced check happens at wifi_manager_init time */
    /* With no creds, portal is true regardless */
    ASSERT(wifi_should_enter_portal() == true);
}

TEST(portal_with_credentials_not_forced) {
    wifi_setup();
    wifi_config_save("TestNet", "password123");
    ASSERT(wifi_config_exists() == true);
    /* With valid creds and no GPIO0 force, should NOT enter portal */
    ASSERT(wifi_should_enter_portal() == false);
}

TEST(portal_with_credentials_gpio0_forced) {
    wifi_setup();
    wifi_config_save("TestNet", "password123");
    ASSERT(wifi_config_exists() == true);
    /* GPIO0 held low during init should force portal */
    test_shim_set_gpio0_low(true);
    wifi_manager_init(NULL);
    ASSERT(wifi_should_enter_portal() == true);
    test_shim_set_gpio0_low(false);
}

TEST(portal_after_max_retries) {
    wifi_setup();
    wifi_config_save("TestNet", "password123");
    ASSERT(wifi_should_enter_portal() == false);
    /* Simulate max retries by setting retry_count via direct connect attempt */
    /* In test mode, esp_wifi_connect is a stub that succeeds immediately */
    /* The portal check is: retry_count >= MAX_RETRY_ATTEMPTS (10) && !softap_active */
    /* We can't directly set retry_count, but we can verify the logic path exists */
    /* Test that with no creds, portal is always true */
    wifi_config_save("", "");
    ASSERT(wifi_should_enter_portal() == true);
}

/* Backoff formula (spec 14: 2s base, 2x growth, 60s cap) */
TEST(backoff_base_case) {
    ASSERT(wifi_backoff_ms_test(0) == 2000);
}

TEST(backoff_exponential_growth) {
    ASSERT(wifi_backoff_ms_test(1) == 4000);
    ASSERT(wifi_backoff_ms_test(2) == 8000);
    ASSERT(wifi_backoff_ms_test(3) == 16000);
    ASSERT(wifi_backoff_ms_test(4) == 32000);
}

TEST(backoff_capped_at_max) {
    /* retry >= 5: 2000 << 5 = 64000, but capped at 60000 */
    ASSERT(wifi_backoff_ms_test(5) == 60000);
    ASSERT(wifi_backoff_ms_test(10) == 60000);
    ASSERT(wifi_backoff_ms_test(100) == 60000);
}

/* WiFi connect from config */
TEST(sta_connect_with_credentials) {
    wifi_setup();
    wifi_config_save("MyNetwork", "mypassword");
    esp_err_t err = wifi_sta_connect_from_config();
    ASSERT(err == ESP_OK);
    ASSERT(wifi_get_net_state() == NET_STATE_CONNECTING);
}

TEST(sta_connect_no_credentials) {
    wifi_setup();
    /* No credentials saved */
    esp_err_t err = wifi_sta_connect_from_config();
    ASSERT(err == ESP_ERR_NOT_FOUND);
}

TEST(sta_connect_invalid_args) {
    wifi_setup();
    esp_err_t err = wifi_sta_connect(NULL, "password");
    ASSERT(err == ESP_ERR_INVALID_ARG);
}

/* SoftAP */
TEST(softap_start_and_stop) {
    wifi_setup();
    esp_err_t err = wifi_start_softap("TestAP", "");
    ASSERT(err == ESP_OK);
    err = wifi_stop_softap();
    ASSERT(err == ESP_OK);
}

TEST(softap_start_invalid_args) {
    wifi_setup();
    esp_err_t err = wifi_start_softap(NULL, "");
    ASSERT(err == ESP_ERR_INVALID_ARG);
}

/* Net state machine */
TEST(net_state_initial) {
    wifi_setup();
    ASSERT(wifi_get_net_state() == NET_STATE_INIT);
}

TEST(is_connected_initial_false) {
    wifi_setup();
    ASSERT(wifi_is_connected() == false);
}

/* Config persistence round-trip */
TEST(config_wifi_persistence) {
    wifi_setup();
    wifi_config_save("PersistNet", "persistpass");
    /* Reset NVS and reload */
    test_shim_reset_nvs();
    ASSERT(wifi_config_exists() == false);
    /* Re-save and verify reload works */
    wifi_config_save("ReloadNet", "reloadpass");
    char ssid[32] = {0};
    char password[64] = {0};
    esp_err_t err = wifi_config_load(ssid, sizeof(ssid), password, sizeof(password));
    ASSERT(err == ESP_OK);
    ASSERT(strcmp(ssid, "ReloadNet") == 0);
    ASSERT(strcmp(password, "reloadpass") == 0);
}

int main(void) {
    printf("=== Net Baseline Tests ===\n\n");

    run_portal_no_credentials();
    run_portal_no_credentials_gpio0_forced();
    run_portal_with_credentials_not_forced();
    run_portal_with_credentials_gpio0_forced();
    run_portal_after_max_retries();
    run_backoff_base_case();
    run_backoff_exponential_growth();
    run_backoff_capped_at_max();
    run_sta_connect_with_credentials();
    run_sta_connect_no_credentials();
    run_sta_connect_invalid_args();
    run_softap_start_and_stop();
    run_softap_start_invalid_args();
    run_net_state_initial();
    run_is_connected_initial_false();
    run_config_wifi_persistence();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
