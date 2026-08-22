#include <stdio.h>

#include "test_fixture_contract.h"

static int tests_run;
static int tests_passed;

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
} while (0)

TEST(valid_contract) {
    const lux_test_fixture_contract_t contract = {
        .name = "artnet_loopback",
        .timeout_ms = 5000,
        .artifact_dir = "artifacts/artnet_loopback",
        .requires_hil = false,
    };
    ASSERT(lux_test_fixture_contract_is_valid(&contract));
}

TEST(hil_contract_is_valid_but_marked) {
    const lux_test_fixture_contract_t contract = {
        .name = "dmx_waveform",
        .timeout_ms = 60000,
        .artifact_dir = "artifacts/dmx_waveform",
        .requires_hil = true,
    };
    ASSERT(lux_test_fixture_contract_is_valid(&contract));
    ASSERT(contract.requires_hil);
}

TEST(rejects_missing_identity) {
    const lux_test_fixture_contract_t no_name = {
        .name = NULL,
        .timeout_ms = 1000,
        .artifact_dir = "artifacts/test",
        .requires_hil = false,
    };
    const lux_test_fixture_contract_t no_artifact = {
        .name = "test",
        .timeout_ms = 1000,
        .artifact_dir = NULL,
        .requires_hil = false,
    };
    ASSERT(!lux_test_fixture_contract_is_valid(&no_name));
    ASSERT(!lux_test_fixture_contract_is_valid(&no_artifact));
}

TEST(rejects_invalid_timeout) {
    const lux_test_fixture_contract_t zero = {
        .name = "test",
        .timeout_ms = 0,
        .artifact_dir = "artifacts/test",
        .requires_hil = false,
    };
    const lux_test_fixture_contract_t too_long = {
        .name = "test",
        .timeout_ms = 600001,
        .artifact_dir = "artifacts/test",
        .requires_hil = false,
    };
    ASSERT(!lux_test_fixture_contract_is_valid(&zero));
    ASSERT(!lux_test_fixture_contract_is_valid(&too_long));
}

int main(void) {
    printf("=== Fixture Contract Tests ===\n\n");
    run_valid_contract();
    run_hil_contract_is_valid_but_marked();
    run_rejects_missing_identity();
    run_rejects_invalid_timeout();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
