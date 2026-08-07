#include "ota.h"
#include "sys_platform.h"

// -- Boot-update --
void otaBootUpdate() {
    // Stub: real implementation reads the factory-OTA boot partition,
    // checks the OTA boot flag, and rolls back if the last app crashed.
}

// -- Server-init --
void initOTA() {
    // Stub: real implementation registers /ota/github, /ota/url,
    // and the async upload handler on the running AsyncWebServer.
}
