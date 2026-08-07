#include "display.h"

bool dispReady = false;

void initDisplay() {
    // Stub: no display hardware wired on the dev board / CI build.
    // On production hardware this would initialise an SSD1306 or similar.
    dispReady = false;
}
