#pragma once
#include <stddef.h>
#include <stdint.h>

// Cross-core lock-free SPSC ring: core-1 DMX task PRODUCES completed ArtRdm
// replies, core-0 netRxTask CONSUMES them via artRdmDrainResponses(). Because the
// two cores race, head/tail are volatile + __sync_synchronize() fences (codebase
// idiom, no libatomic). Replaces the old synchronous rdmRmtRawRelay() call in
// handleArtRdm() which stalled core 0 for up to ~5s during an RDM transaction.
static constexpr size_t ART_RDM_RESP_CAP = 8;
static constexpr size_t ART_RDM_RESP_MAX = 260;

struct ArtRdmResp
{
    uint32_t destIp;
    uint16_t len;
    uint8_t  data[ART_RDM_RESP_MAX];
};

void artRdmRespQueueInit(void);
bool artRdmPushResponse(uint32_t destIp, const uint8_t* data, uint16_t len);  // core 1
bool artRdmRespPop(ArtRdmResp& out);                                          // core 0
