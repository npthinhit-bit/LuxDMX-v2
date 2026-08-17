sACN Packet Queue — Technical Reference

Domain: `net.sacn-pkt-queue`

## 1. Domain Scope

Lock-free single-producer / single-consumer (SPSC) ring buffer that decouples sACN (E1.31) UDP receive from dispatch. The producer (`readSacnSocket`, core 0) enqueues up to 4 packets per socket per 2 ms tick into the ring; the consumer (`readSacn`, same core 0 task) drains them and calls `routeFrame` for each. This bounds the per-tick receive cost and prevents a flood of sACN packets from starving Art-Net receive, the serial console, or the AsyncWebServer task.

**Owns:** the ring storage (`sacnRing[SACN_PKT_CAP]`), producer index (`sacnHead`), consumer index (`sacnTail`), `sacnPktPush`, `sacnPktPop`, `sacnPktQueueInit`.
**Delegates to:** `src/net/sacn.cpp` (producer: `readSacnSocket`, consumer: `readSacn`).
**Consumed by:** `src/core/frame_router.cpp::routeFrame` (via `sacn.cpp::readSacn`).

## 2. Layer Mapping

| Layer | Directory | Role |
|---|---|---|
| net | `src/net/` | Network receive → dispatch pipeline |

## 3. Source Files

| File | Role |
|---|---|
| `src/net/sacn_pkt_queue.h` | Queue struct (`SacnPkt`), capacity constants, function declarations |
| `src/net/sacn_pkt_queue.cpp` | Ring buffer implementation: `sacnPktQueueInit`, `sacnPktPush`, `sacnPktPop` |
| `src/net/sacn.cpp` | Producer (`readSacnSocket`) and consumer (`readSacn`) integration |

## 4. Data Structures

### `SacnPkt` (sacn_pkt_queue.h:6-11)

| Field | Type | Size | Description |
|---|---|---|---|
| `len` | `uint16_t` | 2 B | Packet length in bytes |
| `outIdx` | `uint16_t` | 2 B | Output index this packet maps to (resolved by universe→output) |
| `srcIp` | `uint32_t` | 4 B | Source IP (network byte order) |
| `data` | `uint8_t[638]` | 638 B | Raw sACN packet bytes (E1.31 minimum 638 bytes) |

### Constants (sacn_pkt_queue.h:4-5)

| Name | Value | Description |
|---|---|---|
| `SACN_PKT_CAP` | 16 | Ring capacity |
| `SACN_PKT_MAX` | 638 | Max packet size (E1.31 minimum packet size) |

## 5. Concurrency

Single-threaded within core 0. Both producer (`readSacnSocket`) and consumer (`readSacn`) run in `netRxTask` (core 0, priority 5) at `src/sys/tasks.cpp:146-155`. The `__sync_synchronize()` fence (sacn_pkt_queue.cpp:14,22) follows the codebase idiom for lock-free rings.

SPSC pattern: `h - t >= SACN_PKT_CAP` = full, `h == t` = empty. Indices are `uint32_t` with modulo wrapping (sacn_pkt_queue.cpp:10,20).

## 6. State Machine

No state machine — lock-free FIFO queue. States are implicit: empty (head == tail) or non-empty (head > tail) with a capacity ceiling of 16.

## 7. Entry Points

| Function | Called from | Purpose |
|---|---|---|
| `sacnPktQueueInit()` | `src/net/sacn.cpp` (called during `startSacn`) | Zero head/tail at init |
| `sacnPktPush(outIdx, srcIp, data, len)` | `src/net/sacn.cpp` (`readSacnSocket`) | Producer: enqueue parsed sACN packet |
| `sacnPktPop(SacnPkt&)` | `src/net/sacn.cpp` (`readSacn`) | Consumer: dequeue packet for dispatch |

## 8. Data Flow

1. sACN multicast socket receives packet on UDP 5568
2. `readSacnSocket(outIdx)` reads up to 4 packets per socket per tick, validates the 4-byte root prefix (`0x00 0x10 0x00 0x00`), and enqueues valid packets via `sacnPktPush`
3. `readSacn()` (called from `netRxTask`) drains all sockets' queues via `sacnPktPop` in a loop
4. Each dequeued packet is dispatched to `routeFrame` by the sACN universe, which maps to the output index

## 9. Protocol Layout

N/A (no wire protocol handled here — the queue stores raw sACN packet bytes. The sACN wire protocol is documented in [net-sacn-protocol](./net-sacn-protocol.md).)

## 10. Configuration Integration

None. The queue capacity (`SACN_PKT_CAP = 16`) and max packet size (`SACN_PKT_MAX = 638`) are compile-time constants.

## 11. Lifecycle

1. **Init:** `sacnPktQueueInit()` called during `startSacn()` (multicast group join)
2. **Producer (per 2 ms tick):** `readSacnSocket(int outIdx)` enqueues up to 4 packets per socket
3. **Consumer (same tick):** `readSacn()` drains all socket queues, dispatching via `routeFrame`
4. **No deinit** — static memory, no cleanup needed

## 12. Error Handling

| Condition | Handling | Source |
|---|---|---|
| Ring full | `sacnPktPush` returns `false`; packet dropped | sacn_pkt_queue.cpp:9 |
| Ring empty | `sacnPktPop` returns `false`; consumer loop exits | sacn_pkt_queue.cpp:19 |

## 13. Memory Allocation

Fully static. The ring `sacnRing[SACN_PKT_CAP]` consumes `16 × (2 + 2 + 4 + 638) = 10,272 bytes` of BSS. Producer/consumer indices are plain `uint32_t` (4 bytes each). No heap allocation.

## 14. Timing

**Deadline:** Packets must be drained within the 2 ms `netRxTask` loop period (`src/sys/tasks.cpp:153`). Bounded to 4 packets/socket to prevent starvation of other tasks on core 0.

## 15. Traceability / Evidence

| Claim | Source |
|---|---|
| SPSC ring with head/tail indices | sacn_pkt_queue.h:3-4 |
| Capacity 16, max packet 638 bytes (E1.31 minimum) | sacn_pkt_queue.h:4-5 |
| Producer bounded to 4 packets/socket | src/net/sacn.cpp (readSacnSocket) |
| `__sync_synchronize()` fence used | sacn_pkt_queue.cpp:14,22 |
| Consumer drains per-socket queues | src/net/sacn.cpp (readSacn) |
| sACN wire protocol constants | src/net/sacn.h:6-13 |
| Multicast on UDP 5568 | src/net/sacn.h:22-25 |
| Called from netRxTask on core 0 | src/sys/tasks.cpp:151 |

## 16. Cross-References

- [sACN Protocol](./net-sacn-protocol.md) — the E1.31 wire protocol this queue carries
- [Art Pkt Queue](./net-art-pkt-queue.md) — analogous ring for Art-Net packets
- [Frame Router](./core-frame-router.md) — consumes dispatched sACN frames via `routeFrame`
- [Sender Tracker](./core-sender-tracker.md) — updated by `readSacn` via `updateSender`

## 17. Limitations

- No overflow counter: dropped packets are silently lost.
- Per-socket bound (4 packets) may drop packets during sustained high-frame-rate sACN traffic if the consumer cannot keep up within the 2 ms tick.

## 18. Open Questions

- Not determinable from the inspected source code — whether `SACN_PKT_CAP = 16` was empirically tuned or chosen as a round number.
- Not determinable from the inspected source code — actual packet loss rate under maximum sACN universe count (up to 4 outputs × 4 multicast sockets) at 44 Hz frame rate.

## 19. Testing

No dedicated unit test for `sacn_pkt_queue.cpp`. The ring is exercised indirectly through Art-Net/sACN integration paths, but no isolated host test exists.

## 20. History

No recorded changes.
