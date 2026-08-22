#include <stdio.h>

#include "test_fixture_report.h"

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

static lux_test_fixture_contract_t contract(void) {
    return (lux_test_fixture_contract_t){
        .name = "fixture_report",
        .timeout_ms = 1000,
        .artifact_dir = "artifacts/fixture_report",
        .requires_hil = false,
    };
}

TEST(reports_complete_linear_lifecycle) {
    lux_test_fixture_report_t report;
    lux_test_fixture_contract_t spec = contract();
    ASSERT(lux_test_fixture_report_init(&report, &spec));
    ASSERT(report.phase == LUX_FIXTURE_REPORT_CREATED);
    ASSERT(!lux_test_fixture_report_is_complete(&report));
    ASSERT(lux_test_fixture_report_mark_setup(&report));
    ASSERT(report.phase == LUX_FIXTURE_REPORT_SETUP);
    ASSERT(lux_test_fixture_report_mark_exercised(&report, 2));
    ASSERT(report.phase == LUX_FIXTURE_REPORT_EXERCISED);
    ASSERT(report.artifacts_written == 2);
    ASSERT(lux_test_fixture_report_mark_cleanup(&report));
    ASSERT(lux_test_fixture_report_is_complete(&report));
}

TEST(rejects_out_of_order_transitions) {
    lux_test_fixture_report_t report;
    lux_test_fixture_contract_t spec = contract();
    ASSERT(lux_test_fixture_report_init(&report, &spec));
    ASSERT(!lux_test_fixture_report_mark_exercised(&report, 0));
    ASSERT(!lux_test_fixture_report_mark_cleanup(&report));
    ASSERT(lux_test_fixture_report_mark_setup(&report));
    ASSERT(!lux_test_fixture_report_mark_setup(&report));
    ASSERT(lux_test_fixture_report_mark_exercised(&report, 0));
    ASSERT(!lux_test_fixture_report_mark_exercised(&report, 0));
    ASSERT(lux_test_fixture_report_mark_cleanup(&report));
    ASSERT(!lux_test_fixture_report_mark_cleanup(&report));
}

TEST(rejects_invalid_contract_and_artifact_count) {
    lux_test_fixture_report_t report;
    lux_test_fixture_contract_t invalid = {
        .name = "invalid",
        .timeout_ms = 0,
        .artifact_dir = "artifacts/invalid",
        .requires_hil = false,
    };
    lux_test_fixture_contract_t spec = contract();
    ASSERT(!lux_test_fixture_report_init(&report, &invalid));
    ASSERT(lux_test_fixture_report_init(&report, &spec));
    ASSERT(lux_test_fixture_report_mark_setup(&report));
    ASSERT(!lux_test_fixture_report_mark_exercised(&report, 1025));
    ASSERT(report.phase == LUX_FIXTURE_REPORT_SETUP);
}

int main(void) {
    printf("=== Fixture Report Tests ===\n\n");
    run_reports_complete_linear_lifecycle();
    run_rejects_out_of_order_transitions();
    run_rejects_invalid_contract_and_artifact_count();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
