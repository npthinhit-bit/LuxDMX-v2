#ifndef LUXDMX_OTA_RECOVERY_POLICY_H
#define LUXDMX_OTA_RECOVERY_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#define OTA_BOOT_RETRY_MAX 3u

#ifdef __cplusplus
extern "C" {
#endif

/*
 * boottry stores pending boots already attempted. Return true when the next
 * boot must be rolled back; otherwise return the incremented attempt count.
 */
bool otaRecoveryShouldRollback(uint8_t stored_attempts, uint8_t *next_attempts);

#ifdef __cplusplus
}
#endif

#endif
