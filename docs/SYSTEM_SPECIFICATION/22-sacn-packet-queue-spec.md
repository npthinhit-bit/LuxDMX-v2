# sACN Packet Queue — System Specification

Domain: net.sacn-pkt-queue

## 1. Module Overview

The sACN Packet Queue is a lock-free single-producer / single-consumer (SPSC) ring buffer
that decouples sACN (E1.31) UDP packet reception from frame dispatch. The producer (UDP socket
read) enqueues up to 4 parsed packets per socket per 2 ms tick; the consumer (packet dispatch)
drains them immediately afterward within the same task cycle and dispatches each to the frame
router.

This bounding prevents a flood of sACN multicast traffic from expanding the receive loop
beyond its 2 ms budget, which would starve Art-Net reception, DNS resolution, the web server,
and the serial console on the same core.

Both producer and consumer operate on core 0 within the same network receive task. The
lock-free design is a defensive measure that follows the codebase-wide ring-buffer idiom,
ensuring correctness if the consumer is ever migrated to a separate task or core.

**Owns:** the ring storage (16-entry buffer of packet structs), the producer index, the
consumer index, and the init / push / pop operations.
**Delegates to:** none — the queue is self-contained.
**Consumed by:** the network receive task (core 0) produces parsed packets after socket reads;
the same task drains and dispatches them to the frame router by sACN universe.

## 2. External Interfaces

### 2.1 Entry Points

| Interface | Direction | Purpose |
|---|---|---|
| queueInit | System bring-up (core 0) | Zero the producer and consumer indices at startup. Called during sACN multicast socket initialization. |
| pushPacket | Producer (core 0, socket read) | Enqueue a parsed sACN packet (output index, source IP, data, length). Returns false if the ring is full. |
| popPacket | Consumer (core 0, dispatch) | Dequeue the next packet into the caller's struct. Returns false if the ring is empty. |

### 2.1 Packet Structure

Each ring slot holds a parsed sACN packet with the following fields:

| Field | Type | Description |
|---|---|---|
| length | 16-bit | Packet length in bytes |
| outputIndex | 16-bit | Output index this packet maps to (resolved by universe-to-output mapping) |
| sourceIp | 32-bit | Source IP address (network byte order) |
| data | 638-byte array | Raw sACN packet bytes (E1.31 minimum packet size) |

### 2.3 Capacity Constants

| Constant | Value | Description |
|---|---|---|
| Ring capacity | 16 entries | Total number of packet slots in the ring |
| Max packet size | 638 bytes | Maximum sACN packet size (E1.31 minimum packet) |
| Per-socket read bound | 4 packets | Maximum packets read per socket per 2 ms tick |
| Total static footprint | 10,272 bytes | 16 x (2 + 2 + 4 + 638) bytes in BSS |

## 3. State Machine

No state machine — a lock-free FIFO queue. The ring is always in exactly one of two implicit
conditions: empty (producer index == consumer index) or non-empty (producer index > consumer
index), bounded to a capacity of 16 entries. A ring is full when the difference between the
producer and consumer indices reaches the capacity.

## 4. Data Flow

1. The sACN multicast socket receives a packet on UDP port 5568.
2. The producer (`readSocket`) reads up to 4 packets per socket per 2 ms tick, validates the
   4-byte E1.31 root layer prefix, and resolves the packet's universe to an output index.
3. Each validated packet is pushed into the ring via `pushPacket` with its output index, source
   IP, raw bytes, and length.
4. If the ring is full, the packet is silently dropped (back-pressure); `pushPacket` returns
   false and the call site discards the packet.
5. The consumer (`readPackets`) drains all per-socket queues via a `popPacket` while-loop within
   the same 2 ms tick.
6. Each dequeued packet is dispatched to the frame router by sACN universe, which maps the
   universe to the resolved output index and stages the DMX frame for merging and transmission.

## 5. Configuration Integration

None. The ring capacity (16 entries) and maximum packet size (638 bytes) are compile-time
constants. No configuration engine fields are read or modified by this module.

## 6. Lifecycle

1. **Init:** `queueInit()` is called once during sACN socket initialization, zeroing the
   producer and consumer indices. This occurs when the sACN multicast group is joined at
   network bring-up.
2. **Producer (per 2 ms network receive tick):** For each configured sACN socket, up to 4
   packets are read, validated, and enqueued via `pushPacket`.
3. **Consumer (same tick, after producer):** The dispatch loop drains all per-socket queues via
   a `popPacket` while-loop and dispatches each packet to the frame router.
4. **No deinit** — the ring is static memory; no cleanup is needed.

## 7. Error Handling

| Condition | Handling |
|---|---|
| Ring full on push | `pushPacket` returns false; the packet is silently dropped (back-pressure). |
| Ring empty on pop | `popPacket` returns false; the drain loop exits. |
| No socket ready | The read loop returns early; no packets are enqueued. |
| Invalid packet (short or bad root prefix) | Packet is rejected before enqueue; no slot is consumed. |

All failures are silent at the ring level; callers infer failure from a false return value.
There is no overflow counter or diagnostic metric for dropped packets.

## 8. Timing Constraints

- **Drain deadline:** Packets MUST be drained within the 2 ms network receive task period.
- **Per-socket read bound:** Limited to 4 packets per socket per tick to prevent any single
  high-rate sACN source from monopolizing the core-0 receive budget.
- **Burst capacity:** The 16-entry ring absorbs bursts of up to 4 x 16 = 64 accumulated
  packets across all sACN sockets before back-pressure drops new arrivals.
- **Non-blocking push:** `pushPacket` never blocks; it either succeeds or drops the packet,
  bounding the worst-case receive-loop duration regardless of dispatch speed.

## 9. Memory and Allocation Model

Fully static. The ring occupies 16 x (2 + 2 + 4 + 638) = 10,272 bytes of BSS
(zero-initialized at startup). The producer and consumer indices are 32-bit unsigned integers
with a modulo-wrapping arithmetic and a memory barrier on both access paths to ensure
correctness. No heap allocation occurs, and no PSRAM is used.

## 10. Safety Considerations

- **Burst isolation:** The ring decouples the per-tick receive budget from the dispatch
  workload, preventing a flood of sACN packets from displacing Art-Net DMX frames and
  starving other network-layer processing on the same core.
- **Drop-on-full policy:** When the ring fills, subsequent packets are silently dropped rather
  than blocking the receive loop, keeping all protocol handlers responsive.
- **Per-socket bounding:** The 4-packet-per-socket limit ensures no single sACN source can
  consume the entire 2 ms tick budget, protecting Art-Net reception and web server responsiveness.
- **Same-task serialization:** Producer and consumer run in the same task on core 0,
  guaranteeing no interleaving between enqueue and dequeue operations within a tick.
- **Non-blocking semantics:** `pushPacket` never blocks; it either succeeds or drops the
  packet, bounding the worst-case receive-loop duration regardless of dispatch speed.
- **Frame-size adherence:** The 638-byte maximum matches the E1.31 minimum packet size,
  ensuring the ring cannot be overflowed by an oversized packet.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| net.sacn-protocol | producer and consumer | Enqueues received parsed packets; drains and dispatches by universe |
| core.frame-router | downstream of dispatch | Receives dispatched sACN frames via routeFrame for universe-to-output mapping |
| core.sender-tracker | downstream of dispatch | Updated by the dispatch path with source activity timestamps |
| net.art-packet-queue | sibling | Shares the same core-0 network task budget; the bounded sACN read prevents mutual starvation |
| sys.tasks | schedules | Drives the 2 ms network receive task that contains both producer and consumer |

## 12. Testing Verification

No dedicated unit test exists for this module. The ring is exercised indirectly through the
sACN integration path (multicast socket read, validation, frame dispatch), but no isolated
host test covers push/pop behavior, the full-ring drop policy, or the empty-ring drain
behavior. No host-native test validates the capacity bounds, the back-pressure semantics, or
the per-socket packet limit.

## 13. Open Questions

1. Whether the 16-entry ring capacity was empirically tuned for expected sACN frame rates, or
   chosen as a round number.
2. Whether the per-socket bound of 4 packets per tick is sufficient under maximum sACN
   universe count (up to 4 outputs x multicast sockets) at 44 Hz frame rate, and the actual
   packet loss rate under sustained high-load conditions.
3. Whether the silent drop policy (no overflow counter or diagnostic metric) has been
   acceptable in production under sustained high-frame-rate sACN traffic.

## 14. History

No recorded changes.