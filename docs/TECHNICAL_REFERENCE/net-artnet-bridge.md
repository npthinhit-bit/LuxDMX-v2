ArtNet Bridge — Technical Reference

Domain: `net.artnet-bridge`

## 1. Domain Scope

The Art-Net bridge layer that processes control-plane opcodes received on UDP port 6454. It is invoked by `artHandlePacket` in `artnet.cpp::147` via `artnetBridgeDispatch()` for non-DMX opcodes (poll, sync, address, IP prog, TOD request, RDM). The bridge translates these into firmware actions: responding to ArtPoll, applying address/IP-programming changes, relaying RDM requests to the core-1 RDM task, and committing ArtSync staging.

**Owns:** `sendArtPollReply`, `handleArtAddress`, `handleArtIpProg`, `handleArtTodRequest`, `handleArtRdm`, `artnetBridgeDispatch`.
**Delegates to:** `rdm_task.h::rdmArtRawRelayEnqueue` (RDM requests → core 1 task queue).
**Consumed by:** `artnet.cpp::artHandlePacket` (opcode dispatch at `artnet.cpp:147-236`).

## 2. Layer Mapping

| Layer | Directory | Role |
|---|---|---|
| net | `src/net/` | Art-Net control-plane opcode processing |

## 3. Source Files

| File | Role |
|---|---|
| `src/net/artnet_bridge.cpp` | All bridge handlers: `sendArtPollReply`, `handleArtAddress`, `handleArtIpProg`, `handleArtTodRequest`, `handleArtRdm`, `artnetBridgeDispatch` |
| `src/net/artnet.h` | Opcode constants, `ARTNET_ID`, `ARTNET_PORT`, `artnetBridgeDispatch` declaration |
| `src/net/artnet.cpp` | Caller: `artHandlePacket` dispatches opcodes to the bridge at lines 153, 167 |
| `src/core/rdm_task.h` | `rdmArtRawRelayEnqueue` declaration (RDM relay entry point) |
| `src/core/rdm_task.cpp` | RDM task implementation that processes relayed requests |
| `src/net/art_rdm_resp_queue.h` | Response ring for RDM replies pushed by core 1 |
| `src/net/net_state.h` | `netLocalIP`, `netSubnetMask`, `netGatewayIP` for PollReply |

## 4. Data Structures

### `ArtNetState` (artnet.h:39-57)

| Field | Type | Description |
|---|---|---|
| `artSock` | `int` | UDP socket descriptor on port 6454 (-1 if not ready) |
| `artRdmReady` | `bool` | Socket initialized and bound |
| `nodeIp` | `uint32_t` | Local node IP |
| `nodeMac` | `uint8_t[6]` | WiFi MAC address (used in PollReply) |
| `artRdmEnabled` | `bool` | RDM-over-Art-Net enable flag (from `cfg.artnetRdm`) |
| `artPolls` | `uint16_t` | ArtPoll request counter |
| `bqPolicy` | `uint8_t` | Background queue policy severity (0-4) |
| `bqDirty` | `volatile bool` | Background queue config changed flag |
| `artCfgDirty` | `volatile bool` | Art-Net address config changed flag |
| `syncMode` | `uint8_t` | ArtSync staging mode (0=immediate, 1=staged) |
| `syncLastMs` | `uint32_t` | Last ArtSync timestamp |
| `timecode` | `ArtTimeCode` | Received ArtTimeCode state |
| `timecodeValid` | `bool` | Timecode data is fresh |
| `timecodeSend` | `bool` | Send timecode (from `cfg.timecodeSend`) |
| `timecodeType` | `uint8_t` | Timecode type (from `cfg.timecodeType`) |
| `timecodeFps` | `uint8_t` | Timecode FPS (from `cfg.timecodeFps`) |
| `tcLastSendMs` | `uint32_t` | Last timecode send timestamp |

## 5. Concurrency

Single-threaded on core 0. `artnetBridgeDispatch` is called from `artHandlePacket`, which runs in `netRxTask` (`src/sys/tasks.cpp:150` via `artPktDispatchAll`). No cross-core access within the bridge itself. The RDM relay path (`handleArtRdm`) enqueues to the core-1 RDM task queue, which is lock-free (`xQueueSend`).

## 6. State Machine

The bridge handles discrete opcodes, not a persistent state machine. However, `ArtNetState` maintains two staging state variables:

| Variable | States | Transitions |
|---|---|---|
| `syncMode` | 0 (immediate) → 1 (staged) on `ARTNET_OP_SYNC`? No — `syncMode` is set to `true` elsewhere (ArtSync enable). On `ARTNET_OP_SYNC` receipt: set to `false` and commit staged frames (`artnet.cpp:150-154`) | Not determinable from the inspected source code — where `syncMode` is set to `true` |
| `timecodeValid` | `false` → `true` on `ARTNET_OP_TIMECODE` with n≥13 (`artnet.cpp:194-204`); cleared by timeout | Not determinable from the inspected source code — where `timecodeValid` is set back to `false` |

## 7. Entry Points

| Function | Called from | Opcode | Purpose |
|---|---|---|---|
| `artnetBridgeDispatch(op, p, n, ip)` | `artnet.cpp:153,167` | All non-DMX control opcodes | Top-level dispatch |
| `handleArtPoll(p, n, ip)` | `artnetBridgeDispatch` | 0x2000 (POLL) | Counter ++, send ArtPollReply |
| `handleArtAddress(p, n, ip)` | `artnetBridgeDispatch` | 0x6000 (ADDRESS) | Set universe/net/subnet per output |
| `handleArtIpProg(p, n, ip)` | `artnetBridgeDispatch` | 0xf800 (IPPROG) | Mark config dirty if `cfg.ipProg` |
| `handleArtTodRequest(p, n, ip)` | `artnetBridgeDispatch` | 0x8000 (TODREQUEST) | No-op (stub) |
| `handleArtRdm(p, n, ip)` | `artnetBridgeDispatch` | 0x8300 (RDM) | Enqueue to core-1 RDM task |
| `sendArtPollReply(ip)` | `handleArtPoll` | Internal | Send 240-byte PollReply |

## 8. Data Flow

### ArtPoll → PollReply

1. `artHandlePacket` receives opcode 0x2000 (POLL): `artnet.cpp:165`
2. Dispatches to `artnetBridgeDispatch`: `artnet.cpp:167`
3. `artnetBridgeDispatch` calls `handleArtPoll`: `artnet_bridge.cpp:160`
4. `handleArtPoll` increments poll counter, calls `sendArtPollReply(ip)`: `artnet_bridge.cpp:152-156`
5. `sendArtPollReply` builds a 240-byte reply with node type (0x0100 = dynamic), max outlets, short/long name, local IP, MAC, and per-port status, sends via UDP to the requester: `artnet_bridge.cpp:21-87`

### ArtRdm → Core-1 RDM Task → Core-0 Response

1. `artHandlePacket` receives opcode 0x8300 (RDM): `artnet.cpp:165-168`
2. `artnetBridgeDispatch` dispatches to `handleArtRdm`: `artnet_bridge.cpp:165`
3. `handleArtRdm` extracts the RDM payload (offset 18, max 256 bytes), resolves the line index via `rdmLineForOut[rdmOut]`, and enqueues to core-1: `artnet_bridge.cpp:146-149`
4. `rdmArtRawRelayEnqueue` sends the request (non-blocking `xQueueSend`) with `lineIdx` to the RDM task queue: `rdm_task.h:98`
5. Core-1 RDM task executes `rdmRmtSelect(lineIdx)` → `rdmTx` → `rdmReadFrame`, then pushes the response via `artRdmPushResponse`: core-1 path in `rdm_task.cpp`
6. Core-0 `netRxTask` drains responses via `artRdmDrainResponses()` and sends them as ArtRdm reply packets (opcode 0x8300) back to the requester over the 6454 socket: `artnet.cpp:103-116`

### ArtSync → Staged Commit

1. `artHandlePacket` receives opcode 0x5200 (SYNC): `artnet.cpp:150`
2. Sets `syncMode = false`, calls `commitArtSyncStaged()`: `artnet.cpp:151-152`
3. `commitArtSyncStaged` copies all `staged[i]` buffers into `dmxBufferState().buffers[i].data[1..512]` via seqlock write begin/end: `artnet.cpp:238-249`

## 9. Protocol Layout

### ArtPollReply (240 bytes)

| Offset | Size | Field | Value (from source) |
|---|---|---|---|
| 0-7 | 8 B | ID | `ARTNET_ID` ("Art-Net\0") |
| 8-9 | 2 B | Opcode | 0x0000 (low) / 0x21 (high) → 0x2100 | `artnet_bridge.cpp:25-26` |
| 10 | 1 B | Protocol version high | 14 | `artnet_bridge.cpp:27` |
| 11 | 1 B | Protocol version low | 0 | `artnet_bridge.cpp:28` |
| 12 | 1 B | Answers | 1 | `artnet_bridge.cpp:29` |
| 13 | 1 B | Addresses | 0 | `artnet_bridge.cpp:30` |
| 14 | 1 B | Bind output port | `MAX_OUTPUTS` | `artnet_bridge.cpp:31` |
| 15 | 1 B | Bind input port | 0 | `artnet_bridge.cpp:32` |
| 16-17 | 2 B | Node type | 0x01 (dynamic) / 0x00 | `artnet_bridge.cpp:33-34` |
| 18-21 | 4 B | ESTA manufacturer code | 0x0000 (placeholder) | `artnet_bridge.cpp:37-40` |
| 22-39 | 18 B | Short name | "LuxDMX V2" padded with 0x20 | `artnet_bridge.cpp:43` |
| 40-73 | 34 B | Long name | "LuxDMX V2 Art-Net/sACN to DMX Gateway" padded with 0x20 | `artnet_bridge.cpp:46` |
| 74-137 | 64 B | Node report | "2000:0000:0001:0100 - LuxDMX V2 Ready" padded with 0x20 | `artnet_bridge.cpp:49` |
| 140-141 | 2 B | Bits | 0x0000 | `artnet_bridge.cpp:51-52` |
| 144-145 | 2 B | Wakeup | 0x0000 | `artnet_bridge.cpp:54-55` |
| 148-151 | 4 B | Input port types (SwIn) | 0x00 per port (DMX) | `artnet_bridge.cpp:58` |
| 152-155 | 4 B | Output port types (SwOut) | 0x00 per port (DMX) | `artnet_bridge.cpp:61` |
| 156-159 | 4 B | Local IP | `netLocalIP()` | `artnet_bridge.cpp:63-65` |
| 160-163 | 4 B | Subnet mask | `netSubnetMask()` | `artnet_bridge.cpp:67-69` |
| 164-167 | 4 B | Gateway | `netGatewayIP()` | `artnet_bridge.cpp:71-73` |
| 168-171 | 4 B | Port types (0x80 = DMX) | 0x80 per port | `artnet_bridge.cpp:75` |
| 172-175 | 4 B | Port statuses (0x80 = enabled) | 0x80 if enabled, 0x00 otherwise | `artnet_bridge.cpp:77` |
| 176-181 | 6 B | Node MAC | `g_artNet.nodeMac` | `artnet_bridge.cpp:79` |

### ArtRdm Request (from controller)

| Offset | Size | Field |
|---|---|---|
| 0-7 | 8 B | ID ("Art-Net\0") |
| 8-9 | 2 B | Opcode 0x8300 (little-endian) |
| 10-11 | 2 B | Protocol version (14) |
| 12 | 1 B | Ver (Art-Net version) |
| 13 | 1 B | FAEver (firmware version) |
| 14 | 1 B | Flags (ATC, DMK, etc.) |
| 15 | 1 B | RDM commands |
| 16 | 1 B | Trans# (transaction number) |
| 17 | 1 B | FAEs |
| 18+ | N-18 B | RDM packet (start code + RDM frame) |

RDM payload starts at offset 18 (the RDM start code): `artnet_bridge.cpp:146,149`.

## 10. Configuration Integration

| Config Field | Source | Usage in Bridge | Flags |
|---|---|---|---|
| `output[].universe` | `config_schema.cpp:155` | Set via ArtAddress command 'A': `artnet_bridge.cpp:98` | `CFG_LIVE` |
| `output[].net` | `config_schema.cpp:156` | Set via ArtAddress command 'N': `artnet_bridge.cpp:106` | `CFG_REBOOT` |
| `output[].subnet` | `config_schema.cpp:157` | Set via ArtAddress command 'S': `artnet_bridge.cpp:110` | `CFG_LIVE` |
| `artnetRdm` | `config_schema.cpp:129` | Gate for RDM relay: `artnet_bridge.cpp:133` | `CFG_LIVE` |
| `ipProg` | `config_schema.cpp:114` | Gate for ArtIpProg: `artnet_bridge.cpp:121` | `CFG_LIVE` |
| `useEthernet` | `config_schema.cpp:103` | Not directly in bridge, but affects `netLocalIP()` | `CFG_REBOOT` |

## 11. Lifecycle

1. **Init:** `artRdmInit()` in `artnet.cpp:29` initializes socket, MAC, and bridge state. `artPktQueueInit()` at `artnet.cpp:30` and `artRdmRespQueueInit()` at `artnet.cpp:31`.
2. **Dispatch:** Called every 2 ms tick from `netRxTask` via `artPktDispatchAll()` (`tasks.cpp:150` → `artnet.cpp:88-93`).
3. **Response drain:** `artRdmDrainResponses()` called every 2 ms tick from `netRxTask` (`tasks.cpp:152`).
4. **No deinit.**

## 12. Error Handling

| Condition | Handling | Source |
|---|---|---|
| Invalid RDM packet (n < 24 bytes) | Returns early, no action | `artnet_bridge.cpp:133` |
| No socket (`artSock < 0`) | Returns early | `artnet_bridge.cpp:134` |
| ArtIpProg from non-local subnet | Logs and ignores | `artnet_bridge.cpp:122-124` |
| Unknown opcode | Falls through to default, no-op | `artnet_bridge.cpp:168` |

## 13. Memory Allocation

Fully static. The 240-byte `reply` buffer in `sendArtPollReply` is stack-allocated (`artnet_bridge.cpp:22`). The 576-byte `reply` buffer in `artRdmDrainResponses` is stack-allocated in `artnet.cpp:103`. No heap allocation in the bridge path.

## 14. Timing

**Deadline:** Bridge processing must complete within the 2 ms `netRxTask` loop period (`src/sys/tasks.cpp:153`). Bounded receive (8 packets/tick) ensures the dispatch loop stays within budget.

**RDM relay:** Non-blocking — the core-0 caller (`handleArtRdm`) returns immediately after enqueueing (`artnet_bridge.cpp:149`). The actual RDM transaction runs on core 1 without blocking core 0.

## 15. Traceability / Evidence

| Claim | Source |
|---|---|
| Bridge dispatch table for control opcodes | `artnet_bridge.cpp:158-170` |
| ArtPollReply is 240 bytes with fixed fields | `artnet_bridge.cpp:21-87` |
| RDM requests enqueued to core-1 task (non-blocking) | `artnet_bridge.cpp:146-149` |
| `lineIdx` parameter for multi-output line selection | `artnet_bridge.cpp:148`; `rdm_task.h:98` |
| ArtAddress 'A' sets universe on all outputs | `artnet_bridge.cpp:96-99` |
| ArtAddress 'U' commits and saves config | `artnet_bridge.cpp:101-104` |
| ArtAddress 'N' sets net switch per port | `artnet_bridge.cpp:105-108` |
| ArtAddress 'S' sets subnet per port | `artnet_bridge.cpp:109-112` |
| ArtSync sets syncMode=false and commits staged | `artnet.cpp:150-154` |
| ArtRdm response drain + UDP send (opcode 0x8300) | `artnet.cpp:103-116` |
| Dispatch called from netRxTask | `src/sys/tasks.cpp:150` |

## 16. Cross-References

- [Art-Net Protocol](./net-artnet-protocol.md) — the socket layer (`artRdmPollRx`) and opcode parsing
- [Art Pkt Queue](./net-art-pkt-queue.md) — the SPSC ring that feeds dispatched packets
- [Art RDM Resp Queue](./net-art-rdm-resp-queue.md) — the cross-core response ring
- [RDM Task](./core-rdm-task.md) — core-1 task that processes relayed RDM requests
- [Task Scheduling](./sys-tasks.md) — `netRxTask` on core 0

## 17. Limitations

- `nodeMac` is always the WiFi STA MAC (`esp_wifi_get_mac` at `artnet.cpp:34`); Ethernet MAC is not used when wired mode is active.
- `handleArtTodRequest` is a stub (`artnet_bridge.cpp:129-130`) — TOD (Table of Devices) is not implemented.
- ArtAddress 'T' (Test fade) is a no-op (`artnet_bridge.cpp:113-114`).
- The ESTA manufacturer code is hardcoded 0x0000 (placeholder): `artnet_bridge.cpp:37-40`.
- `syncMode` is set to `true` outside the inspected bridge code — the enable path is not determinable from the inspected source code.

## 18. Open Questions

- Not determinable from the inspected source code — where `syncMode` is set to `true` (the ArtSync enable path).
- Not determinable from the inspected source code — whether `handleArtTodRequest` has a planned implementation.
- Not determinable from the inspected source code — whether the placeholder ESTA code (0x0000) is registered.

## 19. Testing

No dedicated unit test for `artnet_bridge.cpp`. The bridge is exercised through integration testing of the Art-Net receive path. No native host test covers the control-opcode handlers.

## 20. History

No recorded changes.
