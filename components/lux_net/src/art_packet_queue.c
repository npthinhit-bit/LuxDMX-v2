/*
 * spec 20 - Art-Packet Queue
 *
 * Lock-free single-producer / single-consumer (SPSC) ring buffer.
 * Producer core (e.g. the WiFi / UDP receive task) enqueues inbound ArtNet
 * packets; consumer core (the DMX output task) drains them.  Producer and
 * consumer indices only ever move monotonically forward, so unsigned
 * subtraction yields the live fill count without wrap-around ambiguity as
 * long as the queue never holds more than ART_PKT_QUEUE_CAPACITY items -
 * which the push() guard guarantees.
 *
 * Memory-ordering discipline: every cross-core index read/write is paired with
 * __sync_synchronize() (full barrier) so the data copy in the slot cannot be
 * reordered ahead of / hoisted after the visible index update.  This mirrors
 * the cross-core ArtRDM response ring (see response_queue).
 */
#include "art_packet_queue.h"
#include <string.h>

static art_packet_t g_artPktQueue[ART_PKT_QUEUE_CAPACITY];
static volatile uint32_t g_artPktProdIdx = 0;
static volatile uint32_t g_artPktConsIdx = 0;

void artPktQueueInit(void) {
    memset(g_artPktQueue, 0, sizeof(g_artPktQueue));
    __sync_synchronize();
    g_artPktProdIdx = 0;
    g_artPktConsIdx = 0;
    __sync_synchronize();
}

bool artPktQueuePush(const art_packet_t* pkt) {
    if (pkt == NULL) {
        return false;
    }

    uint32_t prod = g_artPktProdIdx;
    uint32_t cons = g_artPktConsIdx;
    __sync_synchronize();

    /* Full: drop the packet (back-pressure signalled via false return). */
    if ((uint32_t)(prod - cons) >= ART_PKT_QUEUE_CAPACITY) {
        return false;
    }

    art_packet_t* slot = &g_artPktQueue[prod % ART_PKT_QUEUE_CAPACITY];
    memcpy(slot, pkt, sizeof(art_packet_t));
    __sync_synchronize();

    g_artPktProdIdx = prod + 1;
    return true;
}

bool artPktQueuePop(art_packet_t* pkt) {
    if (pkt == NULL) {
        return false;
    }

    uint32_t prod = g_artPktProdIdx;
    uint32_t cons = g_artPktConsIdx;
    __sync_synchronize();

    /* Empty: nothing to drain. */
    if (prod == cons) {
        return false;
    }

    const art_packet_t* slot = &g_artPktQueue[cons % ART_PKT_QUEUE_CAPACITY];
    memcpy(pkt, slot, sizeof(art_packet_t));
    __sync_synchronize();

    g_artPktConsIdx = cons + 1;
    return true;
}

bool artPktQueueEmpty(void) {
    uint32_t prod = g_artPktProdIdx;
    uint32_t cons = g_artPktConsIdx;
    __sync_synchronize();
    return prod == cons;
}
