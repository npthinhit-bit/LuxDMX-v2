/*
 * Test: Board configuration table
 * Verifies the canonical board table and template lookup functions.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "shim.h"
#include "boards.h"

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

TEST(board_table_has_expected_boards) {
    size_t count = board_get_count();
    ASSERT(count >= 4);  /* ESP32S3_N16R8, WT32ETH01, ESP32DEV, UNKNOWN */
}

TEST(board_esp32s3_n16r8_correct) {
    const board_def_t* table = board_get_table();
    ASSERT(strcmp(table[BOARD_ESP32S3_N16R8].name, "ESP32-S3-WROOM-2 N16R8") == 0);
    ASSERT(table[BOARD_ESP32S3_N16R8].led_type == LED_TYPE_WS2812);
    ASSERT(table[BOARD_ESP32S3_N16R8].led_pin == 48);
    ASSERT(table[BOARD_ESP32S3_N16R8].net_if == NET_IF_WIFI);
    ASSERT(table[BOARD_ESP32S3_N16R8].has_psram == 1);
}

TEST(board_wt32eth01_correct) {
    const board_def_t* table = board_get_table();
    ASSERT(strcmp(table[BOARD_WT32ETH01].name, "WT32-ETH01") == 0);
    ASSERT(table[BOARD_WT32ETH01].led_type == LED_TYPE_SIMPLE_GPIO);
    ASSERT(table[BOARD_WT32ETH01].led_pin == 2);
    ASSERT(table[BOARD_WT32ETH01].net_if == NET_IF_ETH_RMII);
    ASSERT(table[BOARD_WT32ETH01].has_psram == 0);
}

TEST(board_esp32dev_correct) {
    const board_def_t* table = board_get_table();
    ASSERT(strcmp(table[BOARD_ESP32DEV].name, "ESP32-DevKit") == 0);
    ASSERT(table[BOARD_ESP32DEV].led_type == LED_TYPE_SIMPLE_GPIO);
    ASSERT(table[BOARD_ESP32DEV].led_pin == 2);
    ASSERT(table[BOARD_ESP32DEV].net_if == NET_IF_WIFI);
    ASSERT(table[BOARD_ESP32DEV].has_psram == 0);
}

TEST(template_lookup_esp32dev) {
    ASSERT(board_find_by_template("esp32dev") == BOARD_ESP32DEV);
}

TEST(template_lookup_esp32s3_psram) {
    ASSERT(board_find_by_template("esp32s3_psram") == BOARD_ESP32S3_N16R8);
}

TEST(template_lookup_wt32eth01) {
    ASSERT(board_find_by_template("wt32eth01") == BOARD_WT32ETH01);
}

TEST(template_lookup_unknown_returns_default) {
    ASSERT(board_find_by_template("nonexistent_template") == BOARD_ESP32DEV);
}

TEST(template_lookup_null_returns_unknown) {
    ASSERT(board_find_by_template(NULL) == BOARD_UNKNOWN);
}

TEST(led_type_values) {
    ASSERT(LED_TYPE_OFF == 0);
    ASSERT(LED_TYPE_SIMPLE_GPIO == 1);
    ASSERT(LED_TYPE_WS2812 == 2);
}

TEST(board_capabilities_and_pin_contract) {
    const board_def_t* table = board_get_table();
    ASSERT(board_has_capability(&table[BOARD_ESP32S3_N16R8], BOARD_CAP_WIFI));
    ASSERT(board_has_capability(&table[BOARD_ESP32S3_N16R8], BOARD_CAP_PSRAM));
    ASSERT(board_has_capability(&table[BOARD_ESP32S3_N16R8], BOARD_CAP_WS2812));
    ASSERT(!board_has_capability(&table[BOARD_ESP32S3_N16R8], BOARD_CAP_ETH_RMII));
    ASSERT(table[BOARD_ESP32S3_N16R8].pins.status_led == 48);
    ASSERT(table[BOARD_ESP32S3_N16R8].pins.display_sda == BOARD_PIN_UNASSIGNED);

    ASSERT(board_has_capability(&table[BOARD_WT32ETH01], BOARD_CAP_ETH_RMII));
    ASSERT(table[BOARD_WT32ETH01].pins.rmii_mdc == 23);
    ASSERT(table[BOARD_WT32ETH01].pins.rmii_mdio == 18);
    ASSERT(table[BOARD_WT32ETH01].pins.rmii_power == 16);
    ASSERT(table[BOARD_WT32ETH01].pins.w5500_cs == BOARD_PIN_UNASSIGNED);

    ASSERT(board_has_capability(&table[BOARD_ESP32DEV], BOARD_CAP_WIFI));
    ASSERT(!board_has_capability(&table[BOARD_ESP32DEV], BOARD_CAP_PSRAM));
    ASSERT(table[BOARD_ESP32DEV].pins.status_led == table[BOARD_ESP32DEV].led_pin);
    ASSERT(table[BOARD_UNKNOWN].pins.status_led == BOARD_PIN_UNASSIGNED);
    ASSERT(board_has_capability(NULL, BOARD_CAP_WIFI) == 0);
}

int main(void) {
    printf("=== Board Configuration Tests ===\n\n");

    run_board_table_has_expected_boards();
    run_board_esp32s3_n16r8_correct();
    run_board_wt32eth01_correct();
    run_board_esp32dev_correct();
    run_template_lookup_esp32dev();
    run_template_lookup_esp32s3_psram();
    run_template_lookup_wt32eth01();
    run_template_lookup_unknown_returns_default();
    run_template_lookup_null_returns_unknown();
    run_led_type_values();
    run_board_capabilities_and_pin_contract();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
