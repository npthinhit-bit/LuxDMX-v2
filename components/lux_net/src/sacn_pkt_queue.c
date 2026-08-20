/*
 * spec 22 - sACN Packet Queue
 *
 * Lock-free single-producer / single-consumer (SPSC) ring buffer.
 * Producer (core 0, UDP socket read) enqueues parsed inbound sACN packets;
 * consumer (core 0, same receive task) drains them within the same tick.
 * Producer and consumer indices only ever move monotonically forward, so
 * unsigned subtraction yields the live fill count without wrap-around
 * ambiguity as long as the ring never holds more than SACN_PKT_QUEUE_CAP
 * items - which the push() guard guarantees.
 *
 * Although producer and consumer currently share core 0 in the same task,
 * every index read/write is paired with __sync_synchronize() (full barrier)
 * so the data copy in a slot cannot be reordered ahead of / hoisted after
 * the visible index update. This follows the codebase-wide ring-buffer idiom
 * (see art_packet_queue) and remains correct if the consumer is ever moved
 * to a separate task or core.
 */
#include "sacn.h"
#include <string.h>

static SAcnPacket g_sacnPktQueue[SACN_PKT_QUEUE_CAP];
static volatile uint32_t g_sacnPktProdIdx = 0;
static volatile uint32_t g_sacnPktConsIdx = 0;

void sacn_pkt_queue_init(void) {
    memset(g_sacnPktQueue, 0, sizeof(g_sacnPktQueue));
    __sync_synchronize();
    g_sacnPktProdIdx = 0;
    g_sacnPktConsIdx = 0;
    __sync_synchronize();
}

bool sacn_pkt_queue_push(const SAcnPacket* pkt) {
    if (pkt == NULL) {
        return false;
    }

    uint32_t prod = g_sacnPktProdIdx;
    uint32_t cons = g_sacnPktConsIdx;
    __sync_synchronize();

    /* Full: drop the packet (back-pressure signalled via false return). */
    if ((uint32_t)(prod - cons) >= SACN_PKT_QUEUE_CAP) {
        return false;
    }

    SAcnPacket* slot = &g_sacnPktQueue[prod % SACN_PKT_QUEUE_CAP];
    memcpy(slot, pkt, sizeof(SAcnPacket));
    __sync_synchronize();

    g_sacnPktProdIdx = prod + 1;
    return true;
}

bool sacn_pkt_queue_pop(SAcnPacket* pkt) {
    if (pkt == NULL) {
        return false;
    }

    uint32_t prod = g_sacnPktProdIdx;
    uint32_t cons = g_sacnPktConsIdx;
    __sync_synchronize();

    /* Empty: nothing to drain. */
    if (prod == cons) {
        return false;
    }

    const SAcnPacket* slot = &g_sacnPktQueue[cons % SACN_PKT_QUEUE_CAP];
    memcpy(pkt, slot, sizeof(SAcnPacket));
    __sync_synchronize();

    g_sacnPktConsIdx = cons + 1;
    return true;
}
