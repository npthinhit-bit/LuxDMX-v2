/*
 * Test: Config serial commands
 * Tests the help/dump/get/set/save/wifi/reboot serial console commands.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "shim.h"
#include "config_engine.h"
#include "config_serial.h"
#include "config_schema.h"
#include "logger.h"
#include "boards.h"
#include "hw.h"
#include "nvs.h"
#include "nvs_flash.h"

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

static void teardown(void) {
    test_shim_reset_nvs();
}

TEST(empty_command_returns_ok) {
    setup();
    char response[256];
    esp_err_t err = config_serial_handle_command("", response, sizeof(response));
    ASSERT(err == ESP_OK);
    ASSERT(strcmp(response, "OK") == 0);
    teardown();
}

TEST(help_command_unknown) {
    setup();
    char response[256];
    esp_err_t err = config_serial_handle_command("unknown", response, sizeof(response));
    ASSERT(err == ESP_ERR_NOT_FOUND);
    ASSERT(strstr(response, "ERROR") != NULL);
    teardown();
}

TEST(get_command_returns_value) {
    setup();
    config_set_value("hostname", "test-device");

    char response[256];
    esp_err_t err = config_serial_handle_command("get hostname", response, sizeof(response));
    ASSERT(err == ESP_OK);
    ASSERT(strcmp(response, "test-device") == 0);
    teardown();
}

TEST(get_command_unknown_key) {
    setup();
    char response[256];
    esp_err_t err = config_serial_handle_command("get nonexistent_key", response, sizeof(response));
    ASSERT(err == ESP_ERR_NOT_FOUND);
    ASSERT(strstr(response, "ERROR") != NULL);
    teardown();
}

TEST(set_command_sets_value) {
    setup();
    char response[256];
    esp_err_t err = config_serial_handle_command("set hostname=mydevice", response, sizeof(response));
    ASSERT(err == ESP_OK);
    ASSERT(strcmp(response, "OK") == 0);

    /* Verify the value was set */
    char* value = config_get_value("hostname");
    ASSERT(value != NULL);
    ASSERT(strcmp(value, "mydevice") == 0);
    free(value);
    teardown();
}

TEST(set_command_invalid_value) {
    setup();
    char response[256];
    /* log_level is 0-4, so 99 is invalid */
    esp_err_t err = config_serial_handle_command("set protocol=99", response, sizeof(response));
    ASSERT(err == ESP_ERR_INVALID_ARG);
    ASSERT(strstr(response, "ERROR") != NULL);
    teardown();
}

TEST(dump_command_returns_all_fields) {
    setup();
    char response[1024];
    esp_err_t err = config_serial_handle_command("dump", response, sizeof(response));
    ASSERT(err == ESP_OK);
    /* Should contain known field keys */
    ASSERT(strstr(response, "wifissid=") != NULL);
    ASSERT(strstr(response, "hostname=") != NULL);
    ASSERT(strstr(response, "ledbr=") != NULL);
    /* Secret field present; non-empty secrets masked as ******** */
    ASSERT(strstr(response, "wifipsk=") != NULL);
    char* pw_val = config_get_value("wifipsk");
    if (pw_val && strlen(pw_val) > 0) {
        ASSERT(strstr(response, pw_val) == NULL);
        ASSERT(strstr(response, "********") != NULL);
    }
    if (pw_val) free(pw_val);
}

TEST(save_command_saves_to_nvs) {
    setup();
    config_set_value("hostname", "saved-device");

    char response[256];
    esp_err_t err = config_serial_handle_command("save", response, sizeof(response));
    ASSERT(err == ESP_OK);
    ASSERT(strcmp(response, "OK") == 0);

    /* Reload from NVS and verify */
    config_load();
    char* value = config_get_value("hostname");
    ASSERT(value != NULL);
    ASSERT(strcmp(value, "saved-device") == 0);
    free(value);
    teardown();
}

TEST(reboot_command_requests_reboot) {
    setup();
    ASSERT(config_serial_check_reboot() == false);

    char response[256];
    esp_err_t err = config_serial_handle_command("reboot", response, sizeof(response));
    ASSERT(err == ESP_OK);
    ASSERT(strstr(response, "rebooting") != NULL);

    /* Reboot flag should be set */
    ASSERT(config_serial_check_reboot() == true);

    /* Flag should be cleared after check */
    ASSERT(config_serial_check_reboot() == false);
    teardown();
}

TEST(wifi_command_returns_status) {
    setup();
    char response[256];
    esp_err_t err = config_serial_handle_command("wifi", response, sizeof(response));
    ASSERT(err == ESP_OK);
    ASSERT(strstr(response, "state=") != NULL);
    ASSERT(strstr(response, "connected=") != NULL);
    teardown();
}

TEST(set_command_with_spaces_in_value) {
    setup();
    char response[256];
    /* SSID with spaces */
    esp_err_t err = config_serial_handle_command("set wifissid=My WiFi Network", response, sizeof(response));
    ASSERT(err == ESP_OK);
    ASSERT(strcmp(response, "OK") == 0);

    char* value = config_get_value("wifissid");
    ASSERT(value != NULL);
    ASSERT(strcmp(value, "My WiFi Network") == 0);
    free(value);
    teardown();
}

TEST(board_template_uses_canonical_board_table) {
    setup();
    /* led_pin/led_type now sourced from boards.c board_get_table() (REFACTOR_PLAN §5.2) */
    char* led_pin = config_get_value("ledpin");
    char* led_type = config_get_value("ledtype");
    const board_def_t* tbl = board_get_table();
    board_type_t board = hw_get_board_type();
    ASSERT(led_pin != NULL);
    ASSERT(led_type != NULL);
    ASSERT(tbl != NULL);
    ASSERT((int)board >= 0 && (int)board < (int)board_get_count());
    ASSERT(atoi(led_pin) == tbl[board].led_pin);
    ASSERT(atoi(led_type) == (int)tbl[board].led_type);
    free(led_pin);
    free(led_type);
    teardown();
}


TEST(help_command_prints_help) {
    setup();
    char response[1024];
    esp_err_t err = config_serial_handle_command("help", response, sizeof(response));
    ASSERT(err == ESP_OK);
    ASSERT(strstr(response, "Available commands") != NULL);
    ASSERT(strstr(response, "dump") != NULL);
    ASSERT(strstr(response, "factory") != NULL);
    ASSERT(strstr(response, "list") != NULL);
    teardown();
}

TEST(help_alias_question_mark) {
    setup();
    char response[1024];
    esp_err_t err = config_serial_handle_command("?", response, sizeof(response));
    ASSERT(err == ESP_OK);
    ASSERT(strstr(response, "Available commands") != NULL);
    teardown();
}

TEST(factory_command_resets_and_erases_nvs) {
    setup();
    config_set_value("hostname", "saved-device");
    config_save();
    config_load();
    char* value = config_get_value("hostname");
    ASSERT(value != NULL);
    ASSERT(strcmp(value, "saved-device") == 0);
    free(value);

    char response[256];
    esp_err_t err = config_serial_handle_command("factory", response, sizeof(response));
    ASSERT(err == ESP_OK);
    ASSERT(strcmp(response, "OK factory reset") == 0);

    config_load();
    value = config_get_value("hostname");
    ASSERT(value != NULL);
    ASSERT(strcmp(value, "dmx-gateway") == 0);
    free(value);
    teardown();
}

TEST(list_command_lists_all_keys) {
    setup();
    char response[1024];
    esp_err_t err = config_serial_handle_command("list", response, sizeof(response));
    ASSERT(err == ESP_OK);
    ASSERT(strstr(response, "wifissid") != NULL);
    ASSERT(strstr(response, "hostname") != NULL);
    ASSERT(strstr(response, "ledbr") != NULL);
    teardown();
}

TEST(list_command_with_filter) {
    setup();
    char response[1024];
    esp_err_t err = config_serial_handle_command("list wifi", response, sizeof(response));
    ASSERT(err == ESP_OK);
    ASSERT(strstr(response, "wifissid") != NULL);
    ASSERT(strstr(response, "wifipsk") != NULL);
    ASSERT(strstr(response, "wifimode") != NULL);
    ASSERT(strstr(response, "hostname") == NULL);
    ASSERT(strstr(response, "led_brightness") == NULL);
    teardown();
}

TEST(unknown_command_mentions_help) {
    setup();
    char response[256];
    esp_err_t err = config_serial_handle_command("unknown_cmd", response, sizeof(response));
    ASSERT(err == ESP_ERR_NOT_FOUND);
    ASSERT(strstr(response, "ERROR") != NULL);
    ASSERT(strstr(response, "try help") != NULL);
    teardown();
}
TEST(migration_idempotent) {
    setup();
    nvs_handle_t h;
    esp_err_t err;
    int32_t val;

    err = nvs_open("dmxgw", NVS_READWRITE, &h);
    ASSERT(err == ESP_OK);
    ASSERT(nvs_set_i32(h, "o0_uni", 5) == ESP_OK);
    ASSERT(nvs_set_i32(h, "o1_uni", 10) == ESP_OK);
    ASSERT(nvs_set_i32(h, "apfb", 1) == ESP_OK);
    ASSERT(nvs_commit(h) == ESP_OK);
    nvs_close(h);

    migrateNvsKeys();

    err = nvs_open("dmxgw", NVS_READONLY, &h);
    ASSERT(err == ESP_OK);
    ASSERT(nvs_get_i32(h, "a_uni", &val) == ESP_OK);
    ASSERT(val == 5);
    ASSERT(nvs_get_i32(h, "b_uni", &val) == ESP_OK);
    ASSERT(val == 10);
    ASSERT(nvs_get_i32(h, "fbmode", &val) == ESP_OK);
    ASSERT(val == 1);
    ASSERT(nvs_get_i32(h, "o0_uni", &val) == ESP_ERR_NVS_NOT_FOUND);
    ASSERT(nvs_get_i32(h, "o1_uni", &val) == ESP_ERR_NVS_NOT_FOUND);
    ASSERT(nvs_get_i32(h, "apfb", &val) == ESP_ERR_NVS_NOT_FOUND);
    nvs_close(h);

    migrateNvsKeys();

    err = nvs_open("dmxgw", NVS_READONLY, &h);
    ASSERT(err == ESP_OK);
    ASSERT(nvs_get_i32(h, "a_uni", &val) == ESP_OK);
    ASSERT(val == 5);
    ASSERT(nvs_get_i32(h, "b_uni", &val) == ESP_OK);
    ASSERT(val == 10);
    ASSERT(nvs_get_i32(h, "fbmode", &val) == ESP_OK);
    ASSERT(val == 1);
    nvs_close(h);

    teardown();
}

int main(void) {
    printf("=== Config Serial Command Tests ===\n\n");

    run_empty_command_returns_ok();
    run_help_command_unknown();
    run_get_command_returns_value();
    run_get_command_unknown_key();
    run_set_command_sets_value();
    run_set_command_invalid_value();
    run_dump_command_returns_all_fields();
    run_save_command_saves_to_nvs();
    run_reboot_command_requests_reboot();
    run_wifi_command_returns_status();
    run_set_command_with_spaces_in_value();
    run_board_template_uses_canonical_board_table();
    run_help_command_prints_help();
    run_help_alias_question_mark();
    run_factory_command_resets_and_erases_nvs();
    run_list_command_lists_all_keys();
    run_list_command_with_filter();
    run_unknown_command_mentions_help();
    run_migration_idempotent();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
