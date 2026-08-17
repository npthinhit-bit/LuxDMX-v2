# Art-Net Protocol — Technical Reference

Domain: net.artnet

## 1. Domain Scope

Owns the Art-Net (E1.19) UDP socket on port 6454: packet reception, opcode dispatch, ArtSync frame staging, and ArtRdm reply transmission. Delegates all control-opcode handling (Poll, Address, IPProg, RDM, TodRequest) to [`net-artnet-bridge`](./net-artnet-bridge.md). Delegates DMX data routing to `frame_router`, sender tracking to `sender_tracker`, and buffer writes to `dmx_buffer`. Consumes the parsed-packet ring produced by [`net-art-pkt-queue`](./net-art-pkt-queue.md) and the cross-core RDM reply ring produced by [`net-art-rdm-resp-queue`](./net-art-rdm-resp-queue.md).

## 2. Layer Mapping

`net` layer — network protocol reception and framing. Calls down into `core` (frame_router, dmx_buffer, sender_tracker, scene_engine) and `cfg` (config_schema). Consumed by `sys` (tasks.cpp schedules `artRdmPollRx` / `artPktDispatchAll` / `artRdmDrainResponses`).

## 3. Source Files

| File | Role |
|---|---|
| `src/net/artnet.h` | Constants (`ARTNET_OP_*`, `ARTNET_PORT`, `ARTNET_ID`, `ARTSYNC_TIMEOUT_MS`), `ArtTimeCode` struct, `ArtNetState` struct, accessor/function declarations |
| `src/net/artnet.cpp` | `g_artNet` instance, `artNet()` accessor, `artRdmInit()`, `artRdmPollRx()`, `artPktDispatchAll()`, `artRdmDrainResponses()`, `sendTimecode()`, `artHandlePacket()`, `commitArtSyncStaged()` |

## 4. Data Structures

### ArtTimeCode (artnet.h:29-35)
| Field | Type | Offset | Description |
|---|---|---|---|
| `type` | `uint8_t` | 11 | SMPTE timecode type (0=Film 24, 1=EFG 25, 2=DF 29.97, 3=SMPTE 30) |
| `hour` | `uint8_t` | 12 | Hours (0-23) |
| `minute` | `uint8_t` | 13 | Minutes (0-59) |
| `second` | `uint8_t` | 14 | Seconds (0-59) |
| `frame` | `uint8_t` | 15 | Frame number |

### ArtNetState (artnet.h:39-57)
| Field | Type | Description |
|---|---|---|
| `artSock` | `int` | UDP socket descriptor (-1 = not open) |
| `artRdmReady` | `bool` | Socket + queues initialized |
| `nodeIp` | `uint32_t` | Local IP in network byte order |
| `nodeMac[6]` | `uint8_t[6]` | WiFi MAC address |
| `artRdmEnabled` | `bool` | Whether Art-Net RDM is active (mirrors `cfg.artnetRdm`) |
| `artPolls` | `uint16_t` | Count of ArtPoll requests received |
| `bqPolicy` | `uint8_t` | BackgroundQueue severity policy (4=disabled) |
| `bqDirty` | `volatile bool` | BackgroundQueue policy changed, needs NVS persist |
| `artCfgDirty` | `volatile bool` | Configuration changed via ArtAddress, needs save |
| `syncMode` | `uint8_t` | ArtSync staging mode (0=immediate, 1=sync) |
| `syncLastMs` | `uint32_t` | Last ArtSync timestamp |
| `timecode` | `ArtTimeCode` | Last received timecode |
| `timecodeValid` | `bool` | Whether timecode has been received |
| `timecodeSend` | `bool` | Whether to broadcast timecode output |
| `timecodeType` | `uint8_t` | Output timecode type |
| `timecodeFps` | `uint8_t` | Output timecode frame rate (default 25) |
| `tcLastSendMs` | `uint32_t` | Last timecode broadcast timestamp |

## 5. Concurrency

Single-threaded on core 0 (netRxTask). `g_artNet` is a static instance accessed only from `netRxTask` (core 0, priority 5). The `volatile` qualifiers on `bqDirty`, `artCfgDirty`, `srcStatus`, `rdmSent`, `rdmRecv` are for cross-task reads from `ws_frame.cpp` (loop, core 0), not cross-core. ArtRdm responses arrive from core 1 via the lock-free SPSC ring in [`net-art-rdm-resp-queue`](./net-art-rdm-resp-queue.md), drained by `artRdmDrainResponses()` on core 0.

## 6. State Machine

| State | Trigger | Next State |
|---|---|---|
| `artRdmReady=false` (uninitialized) | `artRdmInit()` succeeds (socket created) | `artRdmReady=true` |
| `artRdmReady=true` (running) | socket creation fails | `artRdmReady=false` (error logged) |
| `syncMode=false` (immediate) | `ARTNET_OP_SYNC` received | `syncMode=true`, staged frames committed |
| `syncMode=true` (staged) | `ARTSYNC_TIMEOUT_MS` (1000 ms) elapsed without SYNC | `syncMode=false`, staged frames committed, fallback to immediate |

## 7. Entry Points

1. `artRdmInit()` — called from `main.cpp:121` during setup phase 7 (Network protocol init). Creates the UDP socket, initializes queues, reads MAC/IP.
2. `artRdmPollRx()` — called from `netRxTask` at `tasks.cpp:149`. Bounded recv loop, 8 packets/tick.
3. `artPktDispatchAll()` — called from `netRxTask` at `tasks.cpp:150`. Drains the `art_pkt_queue` ring.
4. `artRdmDrainResponses()` — called from `netRxTask` at `tasks.cpp:152`. Drains the `art_rdm_resp_queue` ring.

## 8. Data Flow

1. `netRxTask` (core 0) calls `artRdmPollRx()` (artnet.cpp:64) — `recv` at most 8 Art-Net packets/tick into a 640-byte stack buffer (artnet.cpp:66-68, artnet.cpp:70)
2. Each packet validated against `ARTNET_ID` (artnet.cpp:72) and capped at `ART_PKT_MAX` (artnet.cpp:75)
3. Parsed packet pushed to lock-free SPSC ring via `artPktPush()` (artnet.cpp:79) — back-pressure drops on full
4. `artPktDispatchAll()` (artnet.cpp:88) drains the ring, calling `artHandlePacket()` for each (artnet.cpp:91)
5. `artHandlePacket()` (artnet.cpp:147) decodes opcode at `p[8] | (p[9]<<8)` (artnet.cpp:148) and dispatches:
   - `ARTNET_OP_SYNC` → commits staged frames, returns to immediate mode (artnet.cpp:150-155)
   - `ARTNET_OP_POLL`, `ADDRESS`, `IPPROG`, `TODREQUEST`, `RDM` → `artnetBridgeDispatch()` (artnet.cpp:165-169)
   - `ARTNET_OP_DMX` → routes to `frame_router` via `routeFrame()` or stages for ArtSync (artnet.cpp:171-191)
   - `ARTNET_OP_TIMECODE` → stores timecode, triggers scene playback (artnet.cpp:194-205)
   - `ARTNET_OP_TRIGGER` → triggers scene by key (artnet.cpp:208-220)
   - `ARTNET_OP_NZS` → routes nonzero start code to `routeFrameNzs()` (artnet.cpp:222-235)
6. Concurrently, `artRdmDrainResponses()` (artnet.cpp:95) pops completed ArtRdm replies from the core-1 ring and sends them via `lwip_sendto()` back to the originating controller's IP (artnet.cpp:102-117)

## 9. Protocol Layout

Art-Net packet (E1.19):

| Offset | Size | Field |
|---|---|---|
| 0–7 | 8 | ID: `"Art-Net\0"` (ARTNET_ID at artnet.h:20) |
| 8–9 | 2 | Opcode (little-endian) (artnet.cpp:148) |
| 10 | 1 | Protocol version hi (always 14) |
| 11 | 1 | Protocol version lo (always 0) |
| 12+ | variable | Payload (opcode-dependent) |

ArtDMX payload (Opcode 0x5000):

| Offset | Size | Field |
|---|---|---|
| 14–15 | 2 | Universe (artnet.cpp:173) |
| 16–17 | 2 | Length (big-endian) (artnet.cpp:174) |
| 18 | 1 | Sequence (0–255, 0 = ignored) |
| 19 | 1 | Physical port address (0–3) |
| 59 | 1 | Priority (artnet.cpp:177, 0 if packet < 60 bytes → DEFAULT_PRIORITY=100) |
| 60+ | variable | DMX data |

ArtRdm payload (Opcode 0x8300): RDM message starts at offset 18 (artnet.cpp:147, artnet_bridge.cpp:137-140).

ArtTimeCode (Opcode 0x9700): bytes 11–14 carry type/hour/minute/second/frame (artnet.cpp:131-138).

ArtTrigger (Opcode 0x9900): byte 11 = key, byte 12 = subkey (artnet.cpp:210-211). Key 0xFF means use subkey as scene index (artnet.cpp:214).

## 10. Config Integration

Reads from `Config` (`config_schema.h:38-89`, driven by `config_schema.cpp`):

| Field | CFG flags | Live | Used at |
|---|---|---|---|
| `cfg.artnetRdm` (config_schema.cpp:129, artnet.h:46) | `CFG_NONE` | Reboot | `artRdmInit()` artnet.cpp:36; `handleArtRdm()` artnet_bridge.cpp:133 |
| `cfg.timecodeSend` (config_schema.cpp:120, artnet.h:53) | `CFG_NONE` | Reboot | `artRdmInit()` artnet.cpp:37; `sendTimecode()` artnet.cpp:122 |
| `cfg.timecodeType` (config_schema.cpp:121, artnet.h:54) | `CFG_NONE` | Reboot | `artRdmInit()` artnet.cpp:38 |
| `cfg.timecodeFps` (config_schema.cpp:122, artnet.h:55) | `CFG_NONE` | Reboot | `artRdmInit()` artnet.cpp:39; `sendTimecode()` artnet.cpp:124 |
| `cfg.dscpEnabled` (config_schema.cpp:116, config_schema.h:73) | `CFG_NONE` | Reboot | `artRdmInit()` artnet.cpp:46 |
| `cfg.dscpDmx` (config_schema.cpp:117, config_schema.h:74) | `CFG_NONE` | Reboot | `artRdmInit()` artnet.cpp:46-48 |
| `cfg.outputs[i].universe` / `portAddress` | Mixed | Live | `artHandlePacket()` artnet.cpp:181, 189 |
| `cfg.protocol` | `CFG_NONE` | Reboot | `artHandlePacket()` artnet.cpp:172, 223 (protocol 1 = sACN only, skips ArtDMX) |

## 11. Lifecycle

- **Init**: `artRdmInit()` (artnet.cpp:29) — calls `artPktQueueInit()` (artnet.cpp:30) and `artRdmRespQueueInit()` (artnet.cpp:31), binds UDP socket, sets `artRdmReady=true` (artnet.cpp:56). Called from `main.cpp:121`.
- **Poll**: `artRdmPollRx()` → `artPktDispatchAll()` → `artRdmDrainResponses()` — all called every 2 ms tick from `netRxTask` at `tasks.cpp:149-153`.
- **Timecode**: `sendTimecode()` called from `artRdmPollRx()` after recv loop (artnet.cpp:83), rate-limited to `timecodeFps` Hz (artnet.cpp:124).
- **Shutdown**: No explicit shutdown; socket is closed implicitly by the OS at task deletion.

## 12. Error Handling

- Socket creation failure: `g_artNet.artSock < 0`, logged as `"[ART] socket() failed"` (artnet.cpp:58), `artRdmReady` stays false.
- `artRdmPollRx()` early-returns if `!g_artNet.artRdmReady || g_artNet.artSock < 0` (artnet.cpp:65).
- `recvfrom` failure (`n <= 0`): early return from the recv loop (artnet.cpp:71).
- Packet too short (`n < 12` or opcode-specific minimums): silently skipped (artnet.cpp:72, 172, 194, 223).
- `artRdmDrainResponses()` skips empty/malformed responses: `r.len == 0` or socket invalid (artnet.cpp:102).
- `commitArtSyncStaged()` skips disabled/stale outputs (artnet.cpp:240-241).

## 13. Allocation

- `g_artNet` ArtNetState: static data segment (`src/net/artnet.cpp:22`).
- `buf[640]` in `artRdmPollRx()`: static local, stack-reuse pattern (artnet.cpp:66). No heap.
- UDP socket: kernel-managed, file descriptor stored in `g_artNet.artSock`.
- `reply[576]` in `artRdmDrainResponses()`: stack-allocated per drained packet (artnet.cpp:103).
- `pkt[64]` in `sendTimecode()`: stack-allocated (artnet.cpp:128).

No PSRAM allocation. No FreeRTOS heap usage from this module.

## 14. Timing

- **netRxTask period**: 2 ms (`tasks.cpp:153`, `vTaskDelay(pdMS_TO_TICKS(2))`).
- **Packet recv budget**: at most 8 packets per 2 ms tick (artnet.cpp:68).
- **ArtSync timeout**: 1000 ms (`ARTSYNC_TIMEOUT_MS` at artnet.h:74). After this, staged frames commit and the system falls back to immediate mode (artnet.cpp:159, artnet.cpp:161).
- **TimeCode send rate**: `1000 / timecodeFps` ms minimum between sends (artnet.cpp:124), default 25 fps → 40 ms.
- **Socket**: non-blocking (`artnet.cpp:55`), `SO_REUSEADDR` and `SO_BROADCAST` set (artnet.cpp:43-44).

## 15. Traceability

| Claim | File:line |
|---|---|
| Art-Net uses port 6454 | artnet.h:19 |
| ID string is "Art-Net\0" | artnet.h:20 |
| `artRdmInit` initializes queues and socket | artnet.cpp:29-62 |
| `artRdmPollRx` bounded to 8 packets/tick | artnet.cpp:68 |
| Recv into 640-byte buffer | artnet.cpp:66 |
| Packet pushed to ring via `artPktPush` | artnet.cpp:79 |
| `artPktDispatchAll` drains ring | artnet.cpp:88-93 |
| `artRdmDrainResponses` sends opcode 0x8300 | artnet.cpp:106 |
| `sendTimecode` builds 15-byte packet, opcode 0x9700 | artnet.cpp:121-145 |
| Opcode decoding at `p[8] \| (p[9]<<8)` | artnet.cpp:148 |
| ArtSync commits staged + returns to immediate | artnet.cpp:150-154 |
| ArtSync timeout fallback (1000 ms) | artnet.cpp:157-163 |
| POLL/ADDRESS/IPPROG/TODREQUEST/RDM dispatched to bridge | artnet.cpp:165-169 |
| DMX: universe at `p[14-15]`, length at `p[16-17]` | artnet.cpp:173-174 |
| DMX priority at `p[59]` or DEFAULT_PRIORITY | artnet.cpp:177 |
| ArtSync staging path writes to `dmxBufferState().staged` | artnet.cpp:180-184 |
| Immediate DMX routes via `routeFrame` | artnet.cpp:189 |
| TimeCode parsing: type at `p[11]>>5`, hour at `p[11]>>2` | artnet.cpp:196-197 |
| Trigger key 0xFF uses subkey | artnet.cpp:214 |
| NZS: start code at `p[18]`, routes via `routeFrameNzs` | artnet.cpp:228, 233 |
| `commitArtSyncStaged` writes `data[1]` from staged | artnet.cpp:242 |
| `artRdmInit` called from `main.cpp:121` | main.cpp:121 |
| `artRdmPollRx`, `artPktDispatchAll`, `artRdmDrainResponses` in `netRxTask` | tasks.cpp:149-152 |
| DSCP marking: `tos = (dscpDmx & 0x3F) << 2` | artnet.cpp:47 |
| `g_artNet` static instance | artnet.cpp:22 |
| `artNet()` accessor returns reference | artnet.cpp:24 |
| SO_REUSEADDR + SO_BROADCAST | artnet.cpp:43-44 |
| Non-blocking socket set via `O_NONBLOCK` | artnet.cpp:55 |
| `artPktPush`/`artPktPop` implemented in art_pkt_queue | art_pkt_queue.cpp:13-30 |
| `artRdmPushResponse`/`artRdmRespPop` in art_rdm_resp_queue | art_rdm_resp_queue.cpp:13-36 |

## 16. Cross-References

- [`net-art-pkt-queue`](./net-art-pkt-queue.md) — provides `artPktPush`/`artPktPop` used by `artRdmPollRx` (artnet.cpp:79) and `artPktDispatchAll` (artnet.cpp:90)
- [`net-art-rdm-resp-queue`](./net-art-rdm-resp-queue.md) — provides `artRdmPushResponse` (core 1 producer) and `artRdmRespPop` used by `artRdmDrainResponses` (artnet.cpp:101)
- [`net-artnet-bridge`](./net-artnet-bridge.md) — consumes `artnetBridgeDispatch` for POLL/ADDRESS/IPPROG/TODREQUEST/RDM/SYNC opcodes (artnet.cpp:153, 167)
- [`core-frame-router`](./core-frame-router.md) — `routeFrame` / `routeFrameNzs` called from `artHandlePacket` (artnet.cpp:189, 233)
- [`core-dmx-buffer`](./core-dmx-buffer.md) — `dmxBufferState().staged[]` staging and `dmxBufWriteBegin/End` in `commitArtSyncStaged` (artnet.cpp:241-244)
- [`core-sender-tracker`](./core-sender-tracker.md) — `updateSender` called on DMX/STAGED frames (artnet.cpp:186, 245)
- [`core-scene-engine`](./core-scene-engine.md) — `sceneCheckTimecodeTrigger` on TimeCode receive (artnet.cpp:203); `sceneTriggerPlay` on Trigger (artnet.cpp:216)
- [`core-stats`](./core-stats.md) — `stats().fps`, `stats().jitterMs`, `stats().srcStatus` read for WS frame (via ws_frame)
- [`sys-tasks`](./sys-tasks.md) — `netRxTask` (core 0, priority 5) calls `artRdmPollRx`, `artPktDispatchAll`, `artRdmDrainResponses` (tasks.cpp:149-152)
- [`cfg-config-engine`](./config-engine.md) — `cfg.artnetRdm`, `cfg.protocol`, `cfg.outputs[*]` read (artnet.cpp:36, 172, 181)

## 17. Limitations

- ArtPollReply is hardcoded with ESTA manufacturer code 0x0000 (artnet_bridge.cpp:37-40) — not a registered ESTA code.
- TimeCode send is broadcast to 255.255.255.255 (artnet.cpp:143), not unicast to subscribers.
- The 640-byte recv buffer (`buf[640]` at artnet.cpp:66) and the 576-byte reply buffer (artnet.cpp:103) are statically sized; no length negotiation for larger packets.
- sACN discovery packets are consumed and discarded but never sent by this module (sacn.cpp handles outbound).
- ArtTodRequest is accepted by the bridge but the handler is a no-op (artnet_bridge.cpp:129-130).
- `artnetTimecodeStart` / `artnetTimecodeStop` are declared in the header (artnet.h:85-86) but have no implementation in artnet.cpp.

## 18. Open Questions

- Not determinable from the inspected source code — the full ArtPollReply layout (bytes 140-183) has several zeroed reserved fields; the ArtNet specification version this implements is referenced as "14" (artnet.cpp:60, artnet_bridge.cpp:11) but the exact spec revision is not stated in code.
- Not determinable from the inspected source code — `artnetTimecodeStart` and `artnetTimecodeStop` are declared (artnet.h:85-86) but not defined; their intended caller is not visible.

## 19. Testing

No unit test or native test coverage for the Art-Net protocol module. The `config_test`, `seqlock_test`, `merge_test`, and `rdm_types_test` native tests target other domains. Web E2E tests in `docs/` (Playwright) exercise the live protocol against real hardware but do not unit-test `artnet.cpp` directly.

## 20. History

- Core-0 receive loop bounded from 64 to 8 packets/tick to protect the 2 ms `netRxTask` budget (artnet.cpp:67 comment).
- Art-Sync staging path added to support ArtSync / release-time deferred commits (`commitArtSyncStaged` at artnet.cpp:238, `syncMode` field at artnet.h:49).
- RDM response path migrated from synchronous `rdmRmtRawRelay()` (blocking core 0) to the lock-free cross-core SPSC ring via `artRdmPushResponse` / `artRdmRespPop` (artnet.cpp:95-118, art_rdm_resp_queue.h:5-9).
