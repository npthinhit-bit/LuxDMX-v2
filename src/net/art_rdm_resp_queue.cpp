#include "art_rdm_resp_queue.h"
#include <string.h>

static volatile uint32_t rdmRespHead = 0;  // producer (core 1) write index
static volatile uint32_t rdmRespTail = 0;  // consumer (core 0) read index
static ArtRdmResp        rdmRespRing[ART_RDM_RESP_CAP];

void artRdmRespQueueInit(void)
{
    rdmRespHead = 0;
    rdmRespTail = 0;
}

bool artRdmPushResponse(uint32_t destIp, const uint8_t* data, uint16_t len)
{
    if (!data || len == 0)
        return false;
    uint32_t h = rdmRespHead;
    uint32_t t = rdmRespTail;
    if (h - t >= ART_RDM_RESP_CAP)
        return false;  // full: back-pressure
    ArtRdmResp& r    = rdmRespRing[h % ART_RDM_RESP_CAP];
    r.destIp         = destIp;
    uint16_t copyLen = len;
    if (copyLen > ART_RDM_RESP_MAX)
        copyLen = ART_RDM_RESP_MAX;
    memcpy(r.data, data, copyLen);
    r.len       = copyLen;
    rdmRespHead = h + 1;
    __sync_synchronize();
    return true;
}

bool artRdmRespPop(ArtRdmResp& out)
{
    uint32_t t = rdmRespTail;
    uint32_t h = rdmRespHead;
    if (h == t)
        return false;  // empty
    out         = rdmRespRing[t % ART_RDM_RESP_CAP];
    rdmRespTail = t + 1;
    __sync_synchronize();
    return true;
}
