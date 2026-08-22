#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* name;
    uint32_t timeout_ms;
    const char* artifact_dir;
    bool requires_hil;
} lux_test_fixture_contract_t;

static inline bool lux_test_fixture_contract_is_valid(
    const lux_test_fixture_contract_t* contract) {
    if (!contract || !contract->name || contract->name[0] == '\0') {
        return false;
    }
    if (contract->timeout_ms == 0 || contract->timeout_ms > 600000) {
        return false;
    }
    if (!contract->artifact_dir || contract->artifact_dir[0] == '\0') {
        return false;
    }
    return true;
}

#ifdef __cplusplus
}
#endif
