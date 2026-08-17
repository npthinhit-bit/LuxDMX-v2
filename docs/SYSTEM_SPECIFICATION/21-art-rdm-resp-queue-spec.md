# Art RDM Response Queue — System Specification

Domain: net.art-rdm-resp-queue

## 1. Module Overview

The Art RDM Response Queue is a lock-free single-producer / single-consumer (SPSC) ring buffer
that carries completed RDM responses from the core-1 RDM task back to the core-0 network
transmit task for delivery over the Art-Net (UDP port 6454) socket.

The core-1 RDM task produces an `ArtRdmResp` struct after running a full RDM transaction
(RMT transmit followed by UART receive). The core-0 network receive task drains the ring and
sends each response back to the original Art-Net requester as an ArtRdm packet (opcode
0x8300).

This queue replaces the obsolete synchronous relay path in which a core-0 caller would stall
for the full duration of an RDM transaction (up to several seconds), blocking WiFi, AsyncTCP,
and the serial console. The asynchronous queue returns control to core 0 immediately; the
response is delivered by the network task on its next 2 ms cycle.

**Owns:** the ring storage (8-entry buffer of response structs), the producer index, the
consumer index, and the init / push / pop operations.
**Delegates to:** none — the queue is self-contained.
**Consumed by:** the core-1 RDM task (producer) enqueues completed responses; the core-0
network receive task (consumer) drains and transmits them.

## 2. External Interfaces

### 2.1 Entry Points

| Interface | Direction | Purpose |
|---|---|---|
| queueInit | System bring-up (core 0) | Zero the producer and consumer indices at startup. Idempotent. |
| pushResponse | Core-1 RDM task (producer) | Enqueue a completed RDM response (destination IP, length, data). Returns false if the ring is full. |
| popResponse | Core-0 network task (consumer) | Dequeue the next response into the caller's struct. Returns false if the ring is empty. |

### 2.2 Response Structure

Each ring slot holds a completed RDM response with the following fields:

| Field | Type | Description |
|---|---|---|
| destinationIp | 32-bit | IPv4 address of the original Art-Net requester |
| length | 16-bit | Response payload length in bytes (excluding destinationIp) |
| data | 260-byte array | Raw RDM response bytes (start code + RDM frame) |

### 2.3 Capacity Constants

| Constant | Value | Description |
|---|---|---|
| Ring capacity | 8 entries | Total number of response slots in the ring |
| Max response data | 260 bytes | Maximum response payload per entry |
| Total static footprint | 2,128 bytes | 8 x (4 + 2 + 260) bytes in BSS |

## 3. State Machine

No state machine — a lock-free FIFO queue. The ring is always in exactly one of two implicit
conditions: empty (producer index == consumer index) or non-empty (producer index > consumer
index), bounded to a capacity of 8 entries. A ring is full when the difference between the
producer and consumer indices reaches the capacity.

## 4. Data Flow

1. An ArtRdm request arrives on the Art-Net UDP socket and is enqueued to the core-1 RDM task
   via a non-blocking command queue (the RDM task module).
2. The core-1 RDM task selects the target output's RMT channel and DE/RE GPIO, transmits the
   RDM request via RMT, then switches a dedicated RX-only UART to receive the response.
3. The RDM response is read from the UART, completing the transaction.
4. The core-1 RDM task pushes the completed response (destination IP, length, raw bytes) into
   the response ring via `pushResponse`.
5. The core-0 network receive task drains the ring via `popResponse` in a while-loop, taking
   each response and assembling it into an ArtRdm UDP packet (opcode 0x8300).
6. The assembled packet is sent back to the originating requester over the Art-Net socket on
   UDP port 6454.

## 5. Configuration Integration

None. The ring capacity (8 entries) and maximum response size (260 bytes) are compile-time
constants. No configuration engine fields are read or modified by this module.

## 6. Lifecycle

1. **Init:** `queueInit()` is called once during Art-Net socket initialization on core 0,
   zeroing the producer and consumer indices.
2. **Producer (per RDM transaction completion, core 1):** After an RDM transaction completes
   on the core-1 RDM task, the response is pushed into the ring via `pushResponse`.
3. **Consumer (per 2 ms network receive tick, core 0):** The network receive task drains all
   pending entries via a `popResponse` loop and transmits each as an ArtRdm UDP reply.
4. **No deinit** — the ring is static memory; no cleanup is needed.

## 7. Error Handling

| Condition | Handling |
|---|---|
| Invalid or null data, or zero length on push | `pushResponse` returns false; no slot is consumed. |
| Ring full on push | `pushResponse` returns false; the response is silently dropped (back-pressure). |
| Ring empty on pop | `popResponse` returns false; the drain loop exits. |

All failures are silent at the ring level; callers infer failure from a false return value.
There is no retry or requeue mechanism within the ring itself.

## 8. Timing Constraints

- **Drain deadline:** The core-0 network receive task MUST drain all pending responses within
  its 2 ms tick period. The bounded ring (8 entries) absorbs bursts of RDM completions without
  blocking core 1.
- **RDM transaction timing:** Each RDM transaction on core 1 takes approximately 3 ms
  (RMT transmit plus UART receive). During this window, core 0 continues servicing WiFi,
  AsyncTCP, and the serial console uninterrupted — the key improvement over the obsolete
  synchronous path.
- **Non-blocking push:** `pushResponse` never blocks; it either succeeds or drops the
  response, bounding the worst-case completion time on core 1 regardless of how quickly core
  0 drains the ring.

## 9. Memory and Allocation Model

Fully static. The ring occupies 8 x (4 + 2 + 260) = 2,128 bytes of BSS (zero-initialized at
startup). The producer and consumer indices are 32-bit unsigned integers with a volatile
qualifier and a memory barrier on both access paths to ensure cross-core visibility. No heap
allocation occurs, and no PSRAM is used.

## 10. Safety Considerations

- **Core isolation:** The producer (core 1) and consumer (core 0) never access the ring's
  data buffer simultaneously — the SPSC discipline guarantees no torn reads or writes of
  response entries.
- **Burst absorption:** The 8-entry ring decouples the RDM transaction completion rate on
  core 1 from the network drain rate on core 0, preventing a burst of completed transactions
  from stalling the RDM path.
- **Drop-on-full policy:** When the ring fills, subsequent responses are silently dropped
  rather than blocking core 1, ensuring the time-critical DMX transmit and RDM paths remain
  responsive.
- **Memory barrier enforcement:** A memory fence is issued on every producer and consumer
  index access, defending against reordering across the dual-core boundary.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| core.rdm-task | upstream producer | Enqueues completed RDM responses into the ring after each transaction |
| net.artnet-bridge | downstream consumer of responses | Receives relayed ArtRdm replies from the core-0 task and transmits them over UDP |
| sys.tasks | schedules | Drives the 2 ms network receive task that contains the consumer drain loop |
| net.art-packet-queue | sibling | Shares the core-0 network task budget; benefits from bounded, non-blocking ring operations |
| core.rdm-engine | upstream via rdm-task | Provides the RMT transmit and UART receive transport that produces response data |

## 12. Testing Verification

No dedicated unit test exists for this module. The ring is exercised indirectly through the
Art-Net RDM integration path (request arrival, transaction completion, response send-back),
but no isolated host test covers push/pop behavior, the full-ring drop policy, or empty-ring
drain behavior. No host-native test validates the capacity bounds, the back-pressure semantics,
or the cross-core memory barrier correctness.

## 13. Open Questions

1. Whether the 8-entry ring capacity was empirically tuned for expected RDM transaction
   completion rates, or chosen as a round number.
2. Whether dropped responses (ring-full condition) trigger any retry, error log, or diagnostic
   metric to the Art-Net controller, given the silent drop policy.

## 14. History

No recorded changes.