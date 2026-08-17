#pragma once
#include <stddef.h>
#include <stdint.h>

// Lock-free SPSC ring buffer for parsed Art-Net packets. netRxTask (core 0) is the
// single producer (artRdmPollRx recv, bounded to 8/tick) and the single consumer
// (artPktDispatchAll drain). Decoupling them means a burst can never expand the recv
// loop past 8 and starve the rest of the 2ms tick; the ring absorbs bursts across ticks.
static constexpr size_t ART_PKT_CAP = 32;
static constexpr size_t ART_PKT_MAX = 640;

struct ArtPkt
{
    uint16_t len;
    uint32_t srcIp;
    uint8_t  data[ART_PKT_MAX];
};

void artPktQueueInit(void);
bool artPktPush(const ArtPkt& p);  // producer: enqueue; false if full (drop)
bool artPktPop(ArtPkt& out);       // consumer: dequeue; false if empty
