/*
 * Frame Router
 *
 * Entry point for inbound ArtNet / sACN DMX frames. Caches each frame via
 * updateSender(), then dispatches it to every enabled output whose 15-bit
 * port address matches the frame universe. The primary matched output goes
 * through mergeOutput() for priority-filtered blending; split-mask mirrors
 * receive a direct verbatim copy of the frame data.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Route a standard DMX frame (start code 0x00).
 *
 * universe  15-bit port address: net<<8 | sub<<4 | uni
 * data      DMX slot payload (start code already stripped), up to 512 bytes
 * length    number of valid bytes in data
 * senderIp  source IPv4 address (host order)
 * proto     PROTO_ARTNET or PROTO_SACN
 * priority  E1.31 priority 0-255
 */
void routeFrame(uint16_t universe, const uint8_t* data, uint16_t length,
                uint32_t senderIp, uint8_t proto, uint8_t priority);

/* Route a non-zero-start-code (NZS) frame.
 *
 * Identical to routeFrame() except the supplied startCode is written to slot 0
 * of every affected output's live DMX buffer. NZS frames default to
 * PROTO_ARTNET in the sender cache because the start code, not the transport,
 * discriminates the payload (e.g. 0xF0 = RDM).
 *
 * startCode  DMX512 start code (0x01-0xFF)
 */
void routeFrameNzs(uint16_t universe, const uint8_t* data, uint16_t length,
                   uint8_t startCode, uint32_t senderIp, uint8_t priority);

#ifdef __cplusplus
}
#endif
