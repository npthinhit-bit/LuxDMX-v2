#include "sacn_pkt_queue.h"
#include <string.h>
static uint32_t sacnHead = 0;
static uint32_t sacnTail = 0;
static SacnPkt sacnRing[SACN_PKT_CAP];
void sacnPktQueueInit(void) { sacnHead = 0; sacnTail = 0; }
bool sacnPktPush(uint16_t outIdx, uint32_t srcIp, const uint8_t* data, uint16_t len) {
    uint32_t h = sacnHead; uint32_t t = sacnTail;
    if (h - t >= SACN_PKT_CAP) return false;
    SacnPkt& p = sacnRing[h % SACN_PKT_CAP];
    p.outIdx = outIdx; p.srcIp = srcIp; p.len = len;
    memcpy(p.data, data, len);
    sacnHead = h + 1;
    __sync_synchronize();
    return true;
}
bool sacnPktPop(SacnPkt& out) {
    uint32_t t = sacnTail; uint32_t h = sacnHead;
    if (h == t) return false;
    out = sacnRing[t % SACN_PKT_CAP];
    sacnTail = t + 1;
    __sync_synchronize();
    return true;
}
