#include "ota_recovery_policy.h"
#include <stddef.h>

bool otaRecoveryShouldRollback(uint8_t stored_attempts, uint8_t *next_attempts)
{
    if (stored_attempts >= OTA_BOOT_RETRY_MAX) {
        if (next_attempts != NULL) *next_attempts = stored_attempts;
        return true;
    }
    if (next_attempts != NULL) *next_attempts = (uint8_t)(stored_attempts + 1u);
    return false;
}
