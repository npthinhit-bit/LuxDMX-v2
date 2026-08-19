#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "shim.h"
#include "config_engine.h"
#include "logger.h"
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

TEST(migration_idempotent) {
    test_shim_reset_nvs();
    logger_init();
    config_engine_init();
    config_load();

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

    test_shim_reset_nvs();
}

int main(void) {
    printf("=== Config Migration Tests ===\n\n");

    run_migration_idempotent();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
