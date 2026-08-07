#include "firmware_version.h"

const char FIRMWARE_VERSION[]  = "0.0.0-dev";
const char FIRMWARE_BUILD[]    = __DATE__ " " __TIME__;
const char FIRMWARE_VARIANT[]  = "luxdmx_4uni";

void versionCheck() {
    // Stub: real implementation fetches the latest release from
    // github.com/thinhh0321/LuxDMX/releases. For v2 dev builds we skip.
}
