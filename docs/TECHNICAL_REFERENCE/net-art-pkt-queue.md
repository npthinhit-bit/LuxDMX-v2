ArtNet Packet Queue — Technical Reference

Domain: `net.art-pkt-queue`

## 1. Domain Scope

Lock-free single-producer / single-consumer (SPSC) ring buffer that decouples Art-Net UDP receive from protocol dispatch. The producer (`artRdmPollRx`, core 0) enqueues at most 8 parsed packets per 2 ms tick into the ring; the consumer (`artPktDispatchAll`, same core 0 task) drains them. This prevents a burst of Art-Net packets from expanding the recv loop past 8 iterations and starving sACN receive, DNS, and the AsyncWebServer task on the same core.

**Owns:** the ring storage (`artRing[ART_PKT_CAP]`), producer index (`artHead`), consumer index (`artTail`), `artPktPush`, `artPktPop`, `artPktQueueInit`.
**Delegates to:** `artnet.cpp::artRdmPollRx` (producer) and `artnet.cpp::artPktDispatchAll` (consumer).
**Consumed by:** `artHandlePacket` which dispatches to `artnetBridgeDispatch` for control opcodes and `routeFrame`/`routeFrameNzs` for DMX data.

## 2. Layer Mapping

| Layer | Directory | Role |
|---|---|---|
| net | `src/net/` | Network receive → dispatch pipeline |

The queue lives entirely in the `net` layer. It is a transport mechanism between the socket layer (`artnet.cpp`) and the dispatch layer (`artnet_bridge.cpp`).

## 3. Source Files

| File | Role |
|---|---|
| `src/net/art_pkt_queue.h` | Queue struct (`ArtPkt`), capacity constants, function declarations |
| `src/net/art_pkt_queue.cpp` | Ring buffer implementation: `artPktQueueInit`, `artPktPush`, `artPktPop` |
| `src/net/artnet.cpp` | Producer (`artRdmPollRx` at `:64-82`) and consumer (`artPktDispatchAll` at `:86-93`) |

## 4. Data Structures

### `ArtPkt` (art_pkt_queue.h:12-16)

| Field | Type | Offset/Size | Description |
|---|---|---|---|
| `len` | `uint16_t` | 0, 2 B | Parsed packet length in bytes |
| `srcIp` | `uint32_t` | 2, 4 B | Source IP (network byte order from `sockaddr_in`) |
| `data` | `uint8_t[640]` | 6, 640 B | Raw packet bytes (ID + opcode + payload) |

### Constants (art_pkt_queue.h:9-10)

| Name | Value | Description |
|---|---|---|
| `ART_PKT_CAP` | 32 | Ring capacity (32 × 648 B) |
| `ART_PKT_MAX` | 640 | Max parsed packet size (Art-Net max 640 bytes incl. 8-byte ID) |

## 5. Concurrency

Single-threaded within core 0. Both producer (`artRdmPollRx`) and consumer (`artPktDispatchAll`) run in the same FreeRTOS task (`netRxTask`, core 0, priority 5) at `src/sys/tasks.cpp:146-155`. The `__sync_synchronize()` fence at lines 19 and 29 is a defensive double-buffer against future cross-core moves, not a strict requirement today.

SPSC ring index pattern: `h - t >= ART_PKT_CAP` = full, `h == t` = empty. Indices use `uint32_t` and wrap at capacity via modulo (art_pkt_queue.cpp:17,27).

## 6. State Machine

No state machine — lock-free FIFO queue. States are implicit: empty (head == tail) or non-empty (head > tail) with a capacity ceiling.

## 7. Entry Points

| Function | Called from | Purpose |
|---|---|---|
| `artPktQueueInit()` | `src/net/artnet.cpp:30` (`artRdmInit`) | Zero head/tail at init |
| `artPktPush(const ArtPkt&)` | `src/net/artnet.cpp:79` (`artRdmPollRx`) | Producer: enqueue parsed packet; returns `false` if full (packet dropped) |
| `artPktPop(ArtPkt&)` | `src/net/artnet.cpp:90` (`artPktDispatchAll`) | Consumer: dequeue packet for dispatch |

## 8. Data Flow

1. UDP socket receives raw bytes on port 6454 (bounded to 8 packets per 2 ms tick): `artnet.cpp:68-71`
2. Packet is parsed: 8-byte ID check at `artnet.cpp:72`, truncated to `ART_PKT_MAX` at `artnet.cpp:75`
3. Parsed packet is enqueued: `artnet.cpp:79` — `artPktPush(p)`
4. On full ring, the packet is silently dropped (`artPktPush` returns `false`, call site ignores result): `artnet.cpp:79`
5. Consumer drains all queued packets: `artnet.cpp:90` — `artPktPop(p)` in a `while` loop
6. Each dequeued packet is dispatched: `artnet.cpp:91` — `artHandlePacket(p.data, p.len, p.srcIp)`

## 9. Protocol Layout

N/A (no wire protocol — the queue stores raw Art-Net packet bytes verbatim from the UDP socket).

## 10. Configuration Integration

None. The queue capacity (`ART_PKT_CAP = 32`) and max packet size (`ART_PKT_MAX = 640`) are compile-time constants. No `Config` fields are read.

## 11. Lifecycle

1. **Init:** `artPktQueueInit()` called once during `artRdmInit()` at `src/net/artnet.cpp:30`
2. **Producer (per 2 ms tick):** `artRdmPollRx()` at `src/sys/tasks.cpp:149` enqueues up to 8 packets
3. **Consumer (same tick, after producer):** `artPktDispatchAll()` at `src/sys/tasks.cpp:150` drains all packets
4. **No deinit** — the ring is static memory, no cleanup needed

## 12. Error Handling

| Condition | Handling | Source |
|---|---|---|
| Ring full | `artPktPush` returns `false`; packet silently dropped | art_pkt_queue.cpp:16 |
| Ring empty | `artPktPop` returns `false`; caller loop exits | art_pkt_queue.cpp:26 |
| No socket | `artRdmPollRx` returns early | artnet.cpp:65 |

## 13. Memory Allocation

Fully static. The ring `artRing[ART_PKT_CAP]` consumes `32 × (2 + 4 + 640) = 20,608 bytes` of BSS (zero-initialized). Producer/consumer indices are plain `uint32_t` (4 bytes each). No heap allocation.

## 14. Timing

**Deadline:** Packets must be drained within the 2 ms `netRxTask` loop period (`src/sys/tasks.cpp:153`). The bounded recv (8 packets/tick) + ring decoupling ensures bursts don't starve sACN or WebSocket servicing. Ring depth of 32 absorbs bursts of 8×32 = 256 packets.

**Measured behavior:** Not determinable from the inspected source code — would require runtime profiling of packet loss under burst load.

## 15. Traceability / Evidence

| Claim | Source |
|---|---|
| SPSC ring with head/tail indices, producer on core 0 | art_pkt_queue.h:5-6 |
| Capacity 32, max packet 640 bytes | art_pkt_queue.h:9-10 |
| Producer bounded to 8 packets/tick | artnet.cpp:68 |
| Producer pushes parsed packets after ID check | artnet.cpp:72-79 |
| Consumer drains via `while(artPktPop)` loop | artnet.cpp:88-92 |
| Drop-on-full: push returns false, call site ignores | art_pkt_queue.cpp:16; artnet.cpp:79 |
| Init called from `artRdmInit` | artnet.cpp:30 |
| Producer/consumer in same FreeRTOS task | src/sys/tasks.cpp:149-150 |
| `__sync_synchronize()` fence used | art_pkt_queue.cpp:19,29 |

## 16. Cross-References

- [Art-Net Protocol](./net-artnet-protocol.md) — owns the producer (`artRdmPollRx`) and consumer (`artPktDispatchAll`)
- [ArtNet Bridge](./net-artnet-bridge.md) — receives dispatched packets via `artHandlePacket`
- [Art RDM Resp Queue](./net-art-rdm-resp-queue.md) — sibling cross-core ring for RDM responses
- [sACN Packet Queue](./net-sacn-pkt-queue.md) — analogous ring for sACN packets

## 17. Limitations

- No per-packet priority: all packets are equal; a flood of ArtPoll requests can displace DMX frames in the ring.
- No overflow counter: dropped packets are silently lost; there is no diagnostic metric for packet loss at `artnet.cpp:79`.
- Producer and consumer share the same thread (core 0 `netRxTask`); the lock-free design is defensive against a future cross-core move but is not currently cross-core.

## 18. Open Questions

- Not determinable from the inspected source code — whether the silent drop policy (no overflow counter) has been acceptable in production under sustained ArtPoll floods.
- Not determinable from the inspected source code — whether `ART_PKT_CAP = 32` was tuned empirically or chosen as a round number.

## 19. Testing

No dedicated unit test for `art_pkt_queue.cpp`. The ring is exercised indirectly through the Unity unit tests (`test/unit-test/`) and the Art-Net integration path, but the queue itself has no isolated test.

## 20. History

No recorded changes.
