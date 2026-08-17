# Art Packet Queue — System Specification

Domain: net.art-pkt-queue

## 1. Module Overview

The Art Packet Queue is a lock-free single-producer / single-consumer (SPSC) ring buffer that
decouples Art-Net UDP packet reception from protocol dispatch. The producer (network receive)
enqueues at most 8 parsed packets per 2 ms tick; the consumer (packet dispatch) drains them
immediately afterward within the same task cycle. This prevents a burst of Art-Net packets from
expanding the receive loop past its budget and starving sACN reception, DNS, and the web server
on the same core.

The queue operates entirely on core 0 within the network-reception task. Both producer and
consumer run in the same FreeRTOS task, making the lock-free design a defensive measure against
a future cross-core move rather than a strict current requirement.

**Owns:** Ring storage, producer index, consumer index, init/push/pop operations.
**Delegates to:** Art-Net protocol handler (receives parsed packets for dispatch by opcode).
**Consumed by:** Packet dispatch logic that routes by opcode.

## 2. External Interfaces

### 2.1 Entry Points

| Interface | Caller | Direction | Purpose |
|---|---|---|---|
| queueInit() | System bring-up | Init | Zero producer and consumer indices at startup |
| queuePush(packet) | Network receive loop | Producer | Enqueue a parsed packet; returns false if the ring is full |
| queuePop(packet) | Packet dispatch loop | Consumer | Dequeue the next packet; returns false if the ring is empty |

### 2.2 Packet Structure

Each ring slot holds a parsed Art-Net packet with the following fields:

| Field | Type | Description |
|---|---|---|
| length | 16-bit unsigned | Parsed packet length in bytes |
| sourceIp | 32-bit | Source IP address (network byte order) |
| data | 640-byte array | Raw packet bytes (ID + opcode + payload) |

### 2.3 Capacity Constants

| Constant | Value | Description |
|---|---|---|
| Ring capacity | 32 slots | Total number of packet slots in the ring |
| Max packet size | 640 bytes | Maximum parsed Art-Net packet size (including 8-byte ID) |

## 3. State Machine

No state machine — a lock-free FIFO queue. The queue has no internal substates; it is always in
exactly one of two implicit conditions: empty (producer index == consumer index) or non-empty
(producer index > consumer index), with a capacity ceiling of 32 slots.

## 4. Data Flow

1. The UDP socket receives raw bytes on the Art-Net port, bounded to at most 8 packets per
   2 ms tick.
2. Each received packet is validated against the 8-byte "Art-Net" ID string and capped at the
   maximum packet size of 640 bytes.
3. Validated packets are pushed into the ring via queuePush.
4. If the ring is full, the packet is silently dropped (back-pressure); queuePush returns false
   and the call site discards the packet.
5. The consumer drains all queued packets via a queuePop loop within the same 2 ms tick.
6. Each dequeued packet is dispatched by the Art-Net protocol handler, which decodes the opcode
   at byte offset 8 and routes to the appropriate handler (DMX routing, control-opcode bridge,
   timecode, or trigger processing).

## 5. Configuration Integration

None. The ring capacity (32 slots) and maximum packet size (640 bytes) are compile-time
constants. No configuration engine fields are read or modified by this module.

## 6. Lifecycle

1. **Init:** queueInit() is called once during Art-Net socket initialization, zeroing the
   producer and consumer indices.
2. **Producer (per 2 ms tick):** The network receive loop enqueues up to 8 packets. Each packet
   is validated (ID string check, size cap) before enqueue.
3. **Consumer (same tick, after producer):** The dispatch loop drains all queued packets via a
   queuePop while-loop and dispatches each to the protocol handler.
4. **No deinit** — the ring is static memory; no cleanup is needed.

## 7. Error Handling

| Condition | Handling |
|---|---|
| Ring full on push | queuePush returns false; the packet is silently dropped |
| Ring empty on pop | queuePop returns false; the drain loop exits |
| No socket ready | The receive loop returns early; no packets are enqueued |
| Invalid packet (short or bad ID) | Packet is rejected before enqueue; no slot consumed |

## 8. Timing Constraints

- The network-reception task period is 2 ms.
- The receive budget is capped at 8 packets per 2 ms tick.
- The ring capacity of 32 absorbs bursts of up to 8 × 32 = 256 accumulated packets.
- All push and pop operations must complete within the 2 ms tick budget.
- A memory barrier is used on both producer and consumer access to defend against future
  cross-core migration.

## 9. Memory and Allocation Model

Fully static. The ring occupies 32 × (16-bit length + 32-bit source IP + 640-byte data) =
20,608 bytes of BSS (zero-initialized at startup). Producer and consumer indices are plain
32-bit unsigned integers. No heap allocation occurs. No PSRAM is used.

## 10. Safety Considerations

- **Burst isolation:** The ring decouples the receive-loop budget from the dispatch workload,
  preventing a flood of ArtPoll requests from displacing DMX frames and starving sACN
  processing.
- **Drop-on-full policy:** When the ring fills, subsequent packets are silently dropped rather
  than blocking the receive loop, keeping all protocol handlers responsive.
- **Same-task serialization:** Producer and consumer run in the same task on core 0,
  guaranteeing no interleaving between enqueue and dequeue operations within a tick.
- **Non-blocking semantics:** queuePush never blocks; it either succeeds or drops the packet.
  This bounds the worst-case receive-loop duration regardless of dispatch speed.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| net.artnet-protocol | producer and consumer | Enqueues received packets; drains and dispatches by opcode |
| net.artnet-bridge | downstream of dispatch | Processes dispatched control opcodes (Poll, Address, RDM, etc.) |
| core.frame-router | downstream of dispatch | Receives dispatched ArtDMX packets for universe routing |
| core.dmx-buffer | downstream via frame-router | Receives staged DMX frames for ArtSync commits |
| sys.tasks | schedules | Drives the 2 ms network-reception task that contains both producer and consumer |
| net.sacn-protocol | sibling | Shares the same core-0 task budget; benefits from the bounded receive loop |

## 12. Testing Verification

No dedicated unit test exists for the queue. The ring is exercised indirectly through Art-Net
integration tests and the general receive-dispatch pipeline, but no isolated test covers
push/pop behavior, the full-ring drop policy, or empty-ring drain behavior. No host-native test
validates the capacity bounds or the back-pressure semantics.

## 13. Open Questions

1. Whether the silent drop policy (no overflow counter or diagnostic metric) has been
   acceptable in production under sustained ArtPoll floods.
2. Whether the ring capacity of 32 was tuned empirically or chosen as a round number.
3. Whether the memory barrier is strictly necessary given the current same-task producer and
   consumer, or if it was added speculatively for a future cross-core migration.

## 14. History

No recorded changes.
