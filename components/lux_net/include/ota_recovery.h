#ifndef LUXDMX_OTA_RECOVERY_H
#define LUXDMX_OTA_RECOVERY_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "ota_recovery_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Inspect the bootloader rollback state and persist the next retry attempt. */
esp_err_t otaRecoveryInit(void);

/* Mark the current application healthy after the service graph is running. */
esp_err_t otaRecoveryMarkHealthy(void);

bool otaRecoveryPending(void);
uint8_t otaRecoveryAttempts(void);

#ifdef __cplusplus
}
#endif

#endif
