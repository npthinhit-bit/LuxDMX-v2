#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void bridgeDispatch(uint16_t opcode, const uint8_t* pkt, uint16_t len, uint32_t sourceIp);

#ifdef __cplusplus
}
#endif

