#pragma once
#include <stdint.h>
#include <stddef.h>
static constexpr size_t SACN_PKT_CAP = 16;
static constexpr size_t SACN_PKT_MAX = 638;
struct SacnPkt {
    uint16_t len;
    uint16_t outIdx;
    uint32_t srcIp;
    uint8_t  data[SACN_PKT_MAX];
};
void sacnPktQueueInit(void);
bool sacnPktPush(uint16_t outIdx, uint32_t srcIp, const uint8_t* data, uint16_t len);
bool sacnPktPop(SacnPkt& out);
