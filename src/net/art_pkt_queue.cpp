#include "art_pkt_queue.h"
#include <string.h>

static uint32_t artHead = 0;   // producer write index
static uint32_t artTail = 0;   // consumer read index
static ArtPkt artRing[ART_PKT_CAP];

void artPktQueueInit(void) {
    artHead = 0;
    artTail = 0;
}

bool artPktPush(const ArtPkt& p) {
    uint32_t h = artHead;
    uint32_t t = artTail;
    if (h - t >= ART_PKT_CAP) return false;            // full: back-pressure under burst
    artRing[h % ART_PKT_CAP] = p;
    artHead = h + 1;
    __sync_synchronize();                              // codebase lock-free idiom (no libatomic)
    return true;
}

bool artPktPop(ArtPkt& out) {
    uint32_t t = artTail;
    uint32_t h = artHead;
    if (h == t) return false;                          // empty
    out = artRing[t % ART_PKT_CAP];
    artTail = t + 1;
    __sync_synchronize();
    return true;
}
