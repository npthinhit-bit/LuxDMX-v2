#pragma once
#include <stdint.h>

// Poll DMX input on all enabled ports that have inputMode set.
// Called from the netRxTask or main loop on core 0.
void inputRouterPoll();
