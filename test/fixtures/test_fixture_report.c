#include "test_fixture_report.h"

#include <string.h>

#define LUX_FIXTURE_MAX_ARTIFACTS 1024u

bool lux_test_fixture_report_init(
    lux_test_fixture_report_t* report,
    const lux_test_fixture_contract_t* contract) {
    if (!report || !contract || !lux_test_fixture_contract_is_valid(contract)) {
        return false;
    }
    memset(report, 0, sizeof(*report));
    report->contract = *contract;
    report->phase = LUX_FIXTURE_REPORT_CREATED;
    return true;
}

bool lux_test_fixture_report_mark_setup(lux_test_fixture_report_t* report) {
    if (!report || report->phase != LUX_FIXTURE_REPORT_CREATED) {
        return false;
    }
    report->phase = LUX_FIXTURE_REPORT_SETUP;
    return true;
}

bool lux_test_fixture_report_mark_exercised(
    lux_test_fixture_report_t* report,
    uint32_t artifacts_written) {
    if (!report || report->phase != LUX_FIXTURE_REPORT_SETUP ||
        artifacts_written > LUX_FIXTURE_MAX_ARTIFACTS) {
        return false;
    }
    report->artifacts_written = artifacts_written;
    report->phase = LUX_FIXTURE_REPORT_EXERCISED;
    return true;
}

bool lux_test_fixture_report_mark_cleanup(lux_test_fixture_report_t* report) {
    if (!report || report->phase != LUX_FIXTURE_REPORT_EXERCISED) {
        return false;
    }
    report->phase = LUX_FIXTURE_REPORT_CLEANED;
    return true;
}

bool lux_test_fixture_report_is_complete(
    const lux_test_fixture_report_t* report) {
    return report && report->phase == LUX_FIXTURE_REPORT_CLEANED;
}
