Art RDM Response Queue — Technical Reference

Domain: `net.art-rdm-resp-queue`

## 1. Domain Scope

Cross-core lock-free SPSC ring buffer that carries completed RDM responses from core 1 (DMX task) to core 0 (netRxTask / Art-Net socket sender). Core 1 produces `ArtRdmResp` structs after running an RDM transaction (RMT TX + UART RX); core 0 drains the ring and sends each response back to the original Art-Net requester over the 6454 UDP socket.

This queue replaces the old synchronous `rdmRmtRawRelay()` call in `handleArtRdm()` which stalled core 0 for up to ~5 seconds during an RDM transaction, blocking WiFi, AsyncTCP, and the serial console.

**Owns:** the ring storage (`rdmRespRing[ART_RDM_RESP_CAP]`), producer index (`rdmRespHead`), consumer index (`rdmRespTail`), `artRdmPushResponse`, `artRdmRespPop`, `artRdmRespQueueInit`.
**Delegates to:** `art_rdm_resp_queue.cpp` (implementation) and `src/net/artnet.cpp:95-118` (consumer drain + UDP send).
**Consumed by:** core 1 RDM task (`rdmTask.cpp`) pushes responses; core 0 `netRxTask` (`tasks.cpp:152`) drains and sends them.

## 2. Layer Mapping

| Layer | Directory | Role |
|---|---|---|
| net | `src/net/` | Cross-core RDM response relay (part of the `net` layer despite being driven by the `core` RDM task) |

The queue definition lives in `net/` because the response is an Art-Net (network) response. The producer is the core-1 RDM task, but the ring and its drain/send logic are network-layer concerns.

## 3. Source Files

| File | Role |
|---|---|
| `src/net/art_rdm_resp_queue.h` | Ring struct (`ArtRdmResp`), capacity constants, function declarations |
| `src/net/art_rdm_resp_queue.cpp` | Ring implementation: `artRdmRespQueueInit`, `artRdmPushResponse`, `artRdmRespPop` |
| `src/net/artnet.cpp` | Consumer drain + UDP send: `artRdmDrainResponses()` at lines 95-118 |
| `src/core/rdm_task.h` | Producer enqueue interface: `rdmArtRawRelayEnqueue()` at line 98 |
| `src/core/rdm_task.cpp` | Producer implementation: pushes via `artRdmPushResponse` |
| `src/sys/tasks.cpp` | Consumer invoked from `netRxTask`: `artRdmDrainResponses()` at line 152 |

## 4. Data Structures

### `ArtRdmResp` (art_rdm_resp_queue.h:13-17)

| Field | Type | Size | Description |
|---|---|---|---|
| `destIp` | `uint32_t` | 4 B | Destination IP of the original ArtNet RDM requester |
| `len` | `uint16_t` | 2 B | Response payload length (excluding `destIp`) |
| `data` | `uint8_t[260]` | 260 B | Raw RDM response bytes (start code + RDM frame, up to 256 B + 4 B header) |

### Constants (art_rdm_resp_queue.h:10-11)

| Name | Value | Description |
|---|---|---|
| `ART_RDM_RESP_CAP` | 8 | Ring capacity (8 × 266 B = 2,128 B) |
| `ART_RDM_RESP_MAX` | 260 | Max response data per entry |

## 5. Concurrency

**Cross-core SPSC.** Producer: core 1 `dmxTxTask`/`rdmTask` pushes via `artRdmPushResponse`. Consumer: core 0 `netRxTask` pops via `artRdmRespPop`. Both indices are `volatile` with `__sync_synchronize()` fences (art_rdm_resp_queue.cpp:5-6, 25, 35). The codebase avoids `libatomic` — the `__sync_*` idiom is used instead (art_rdm_resp_queue.cpp:25,35).

Full ring behavior: `h - t >= ART_RDM_RESP_CAP` = full (drop), `h == t` = empty.

## 6. State Machine

No state machine — lock-free FIFO ring. States are implicit: empty (head == tail) or non-empty (head > tail) with capacity 8.

## 7. Entry Points

| Function | Called from | Core | Purpose |
|---|---|---|---|
| `artRdmRespQueueInit()` | `src/net/artnet.cpp:31` (`artRdmInit`) | 0 | Zero head/tail at init |
| `artRdmPushResponse(destIp, data, len)` | `src/net/ota.cpp`-style core-1 path (`rdm_task.cpp`) | 1 | Producer: enqueue completed RDM response |
| `artRdmRespPop(ArtRdmResp&)` | `src/net/artnet.cpp:101` (`artRdmDrainResponses`) | 0 | Consumer: dequeue response for UDP send |

## 8. Data Flow

1. Core 0 receives an ArtRdm request: `handleArtRdm()` in `artnet_bridge.cpp:132-150`
2. Request is enqueued to the core-1 RDM task: `rdmArtRawRelayEnqueue()` at `rdm_task.h:98`, non-blocking `xQueueSend` with `lineIdx` parameter
3. Core 1 RDM task processes the request: runs `rdmRmtSelect(cmd.lineIdx)` → `rdmTx()` → `rdmReadFrame()` (full RDM transaction, ~3 ms)
4. Core 1 pushes the completed response: `artRdmPushResponse(destIp, rdmData, len)` — fills the ring entry and bumps `rdmRespHead`
5. Core 0 `netRxTask` drains the ring: `artRdmDrainResponses()` at `artnet.cpp:95-118`
6. Each response is sent as an ArtRdm packet (opcode 0x8300) back to the original requester over the 6454 socket: `artnet.cpp:103-116`

## 9. Protocol Layout

The queue stores raw RDM response bytes (no protocol parsing). The response packet assembled for UDP send is documented in the Art-Net protocol module (see [net-artnet-protocol](./net-artnet-protocol.md) Section 9). The ring entry stores the RDM payload only; the Art-Net envelope is assembled at drain time in `artnet.cpp:103-116`.

## 10. Configuration Integration

None. The ring capacity and max response size are compile-time constants. No `Config` fields are read.

## 11. Lifecycle

1. **Init:** `artRdmRespQueueInit()` called from `artRdmInit()` at `src/net/artnet.cpp:31`
2. **Producer lifecycle:** invoked per RDM transaction completion on core 1 (`rdm_task.cpp`)
3. **Consumer lifecycle:** `artRdmDrainResponses()` is called every 2 ms tick in `netRxTask` at `src/sys/tasks.cpp:152`
4. **No deinit** — static memory, no cleanup needed

## 12. Error Handling

| Condition | Handling | Source |
|---|---|---|
| Invalid/null data or zero length | `artRdmPushResponse` returns `false` | art_rdm_resp_queue.cpp:14 |
| Ring full | `artRdmPushResponse` returns `false`; response dropped | art_rdm_resp_queue.cpp:17 |
| Ring empty | `artRdmRespPop` returns `false`; drain loop exits | art_rdm_resp_queue.cpp:32 |

## 13. Memory Allocation

Fully static. The ring `rdmRespRing[ART_RDM_RESP_CAP]` consumes `8 × (4 + 2 + 260) = 2,128 bytes` of BSS. The `volatile` qualifier on head/tail indices ensures cross-core visibility without heap allocation. No `heap_caps` or PSRAM usage.

## 14. Timing

**Deadline:** Core 0 must drain responses within the 2 ms `netRxTask` loop period (`src/sys/tasks.cpp:153`). The bounded ring (8 entries) absorbs bursts of RDM completions without blocking core 1.

**RDM transaction timing:** Each RDM transaction on core 1 takes ~3 ms (RMT TX + 9 ms UART RX timeout for the response). During this window, core 0 continues servicing WiFi, AsyncTCP, and the serial console uninterrupted — the key improvement over the old synchronous path.

## 15. Traceability / Evidence

| Claim | Source |
|---|---|
| Cross-core SPSC ring: core 1 produces, core 0 consumes | art_rdm_resp_queue.h:5-6 |
| `volatile` + `__sync_synchronize()` for cross-core safety | art_rdm_resp_queue.h:7-8; cpp:25,35 |
| Replaces synchronous `rdmRmtRawRelay()` which stalled core 0 | art_rdm_resp_queue.h:8-9 |
| Init in `artRdmInit` | artnet.cpp:31 |
| Consumer drains every 2 ms tick | src/sys/tasks.cpp:152 |
| Drain sends ArtRdm reply (opcode 0x8300) back to requester | artnet.cpp:103-116 |
| Producer: `rdmArtRawRelayEnqueue` on core 0 → core 1 task queue | rdm_task.h:98 |
| `lineIdx` parameter for multi-output line selection | rdm_task.h:98, 44 |

## 16. Cross-References

- [Art Pkt Queue](./net-art-pkt-queue.md) — sibling SPSC ring for Art-Net control packets (same core)
- [RDM Task](./core-rdm-task.md) — core 1 task that produces responses via `rdmArtRawRelayEnqueue`
- [RDM Engine](./core-rdm-engine.md) — `rdmTx`/`rdmReadFrame` transport that completes the transaction
- [Art-Net Protocol](./net-artnet-protocol.md) — the ArtRdm opcode (0x8300) and wire format
- [ArtNet Bridge](./net-artnet-bridge.md) — `handleArtRdm` enqueues to the core-1 task
- [Task Scheduling](./sys-tasks.md) — `netRxTask` consumer at `tasks.cpp:146-155`

## 17. Limitations

- Capacity of 8 may be insufficient under high RDM-over-Art-Net load: if core 1 completes >8 responses while core 0 is blocked (e.g., by a slow UDP send), the newest response is silently dropped.
- No overflow counter or diagnostic metric for dropped responses.

## 18. Open Questions

- Not determinable from the inspected source code — whether 8-entry capacity was empirically tuned for expected RDM transaction rates.
- Not determinable from the inspected source code — whether dropped responses trigger any retry or error reporting to the Art-Net controller.

## 19. Testing

No dedicated unit test for `art_rdm_resp_queue.cpp`. The ring is exercised through the Art-Net RDM integration path, but no isolated host test exists.

## 20. History

No recorded changes.
