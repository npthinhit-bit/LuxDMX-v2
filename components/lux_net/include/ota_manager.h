#pragma once

#include "esp_http_server.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_PHASE_IDLE = 0,
    OTA_PHASE_WRITING = 1,
    OTA_PHASE_FINALIZING = 2,
    OTA_PHASE_ERROR = 3,
} ota_phase_t;

void otaManagerInit(void);
esp_err_t otaManagerRegister(httpd_handle_t server);
ota_phase_t otaManagerPhase(void);
uint8_t otaManagerProgress(void);
const char *otaManagerTarget(void);

#ifdef __cplusplus
}
#endif
