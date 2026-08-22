#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "test_fixture_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LUX_FIXTURE_REPORT_CREATED = 0,
    LUX_FIXTURE_REPORT_SETUP,
    LUX_FIXTURE_REPORT_EXERCISED,
    LUX_FIXTURE_REPORT_CLEANED,
} lux_fixture_report_phase_t;

typedef struct {
    lux_test_fixture_contract_t contract;
    lux_fixture_report_phase_t phase;
    uint32_t artifacts_written;
} lux_test_fixture_report_t;

bool lux_test_fixture_report_init(
    lux_test_fixture_report_t* report,
    const lux_test_fixture_contract_t* contract);

bool lux_test_fixture_report_mark_setup(lux_test_fixture_report_t* report);

bool lux_test_fixture_report_mark_exercised(
    lux_test_fixture_report_t* report,
    uint32_t artifacts_written);

bool lux_test_fixture_report_mark_cleanup(lux_test_fixture_report_t* report);

bool lux_test_fixture_report_is_complete(
    const lux_test_fixture_report_t* report);

#ifdef __cplusplus
}
#endif
