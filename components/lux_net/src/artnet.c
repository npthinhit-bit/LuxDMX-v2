/*
 * spec 20 - Art-Packet Queue
 *
 * ArtNet protocol handler entry point (stub).  The full inbound UDP / ArtPoll /
 * ArtDMX dispatch pipeline is implemented in a later phase; for now this only
 * provides the init hook so the lux_net component links cleanly and emits a
 * boot-time log marker.
 */
#include "logger.h"

void artnet_init(void) {
    LOG_INFO("artnet", "artnet handler initialized (stub)");
}
