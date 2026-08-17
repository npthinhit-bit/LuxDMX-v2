# Frame Router — Technical Reference

Domain: core.frame-router

## 1. Domain Scope

Owns the routing of a single received DMX universe frame to every enabled output whose 15-bit port address matches. The router is the central dispatch point between network packet handlers (Art-Net/sACN, both on core 0) and the downstream merge engine + seqlock buffer (core 1).

For each incoming frame the router:
1. Updates the sender tracker with the new frame data.
2. Writes the frame into the seqlock-protected buffer of every matching output via `dmxBufWriteBegin/WriteEnd`.
3. Invokes `mergeOutput()` for each matched output.
4. Optionally logs the frame if the output is the "monitor" output (viewed by WebSocket).
5. Mirrors the frame to additional outputs via the `splitMask` bitmask (universe splitting).
6. Updates `stats().srcStatus` via `sourceStatus()`.

Delegates sender tracking to `[core-sender-tracker](./core-sender-tracker.md)`, buffer writes to `[core-dmx-buffer](./core-dmx-buffer.md)`, merging to `[core-merge-engine](./core-merge-engine.md)`, and output view selection to `[core-output-init](./core-output-init.md)`.

Consumers:
- `[net-artnet-protocol](./net-artnet-protocol.md):189,233` — calls `routeFrame()` and `routeFrameNzs()`.
- `[net-sacn-protocol](./net-sacn-protocol.md):208,244,271` — calls `routeFrame()` for direct frames and sync-loss commits.
- `src/sys/tasks.cpp:131` — `snapshotAndTransmit` reads `viewOutput()` indirectly via `frame_router.cpp:24`.

## 2. Layer Mapping

```
drv  →  cfg  →  core  →  net  →  app/sys
                    ↑      ↑      ↑
                    |      |      |
                    |      calls —|—— routeFrame() / routeFrameNzs()
                    |            (core 0, netRxTask)
                    |
                    writes buffers (core 1 reads via
                    snapshotAndTransmit)
```

The frame router is a **core** layer module called by **net** layer consumers (`artnet.cpp`, `sacn.cpp`) on core 0. It writes to the **core** layer DMX buffer (read by **sys** layer `dmxTxTask` on core 1).

## 3. Source Files

| File | Role |
|---|---|
| `src/core/frame_router.h` | `portAddress()` inline (line 8), `routeFrame()` declaration (line 15), `routeFrameNzs()` declaration (line 20) |
| `src/core/frame_router.cpp` | `routeFrameImpl()` (line 12), `routeFrame()` (line 42), `routeFrameNzs()` (line 47) |
| `include/config_schema.h:11-36` | `DmxOutput` struct — `universe`, `net`, `subnet`, `splitMask`, `enabled` fields read by the router |
| `src/core/sender_tracker.h:32-33` | `updateSender()` called by the router |
| `src/core/merge_engine.h:13` | `mergeOutput()` called by the router |
| `src/core/dmx_buffer.h:32-33` | `dmxBufWriteBegin/WriteEnd` inline writers used by the router |
| `src/core/output_init.h:14` | `viewOutput()` called for monitor logging |
| `src/core/stats.h:42,59` | `stats().srcStatus` and `stats().rxFrameCount` updated by the router |

## 4. Data Structures

### `portAddress` (`src/core/frame_router.h:8-10`)

Computes the 15-bit Art-Net port address from a `DmxOutput`:

```c
(net << 8) | (subnet << 4) | universe
```

This maps the three-field Art-Net addressing (net 0-127, subnet 0-15, universe 0-15) into the single universe number used on the wire.

### `splitMask` field (`include/config_schema.h:34`)

| Field | Type | Description |
|---|---|---|
| `splitMask` | `int` (`include/config_schema.h:34`) | Bitmask of additional output indices (0-based) that should receive a copy of this output's frame. Bit `j` set means output `j` mirrors output `i`. |

### Inputs read per output

| DmxOutput field | Location | Read in |
|---|---|---|
| `enabled` | `include/config_schema.h:12` | `src/core/frame_router.cpp:17,29` |
| `universe` | `include/config_schema.h:13` | `portAddress()` via `src/core/frame_router.h:9` |
| `net` | `include/config_schema.h:14` | `portAddress()` via `src/core/frame_router.h:9` |
| `subnet` | `include/config_schema.h:15` | `portAddress()` via `src/core/frame_router.h:9` |
| `splitMask` | `include/config_schema.h:34` | `src/core/frame_router.cpp:26` |

### Outputs updated

| Target | Location | Updated in |
|---|---|---|
| `buffers[i].data[0]` (start code) | `src/core/dmx_buffer.h:12` | `src/core/frame_router.cpp:20,31` |
| `buffers[i].data[1..512]` (slots) | `src/core/dmx_buffer.h:12` | `src/core/frame_router.cpp:21,32` |
| `stats().rxFrameCount[i]` | `src/core/stats.h:34` | `src/core/frame_router.cpp:18` |
| `stats().srcStatus` | `src/core/stats.h:42` | `src/core/frame_router.cpp:39` |

## 5. Concurrency

**Single-threaded (core 0, `netRxTask`, priority 5).**

- `routeFrame()` and `routeFrameNzs()` are called from `artHandlePacket()` (`src/net/artnet.cpp:189,233`) and `handleSacnPacket()` (`src/net/sacn.cpp:207-208`), both of which run in `netRxTask` on core 0 (`src/sys/tasks.cpp:149-151`).
- The router writes to the seqlock-protected DMX buffer via `dmxBufWriteBegin/WriteEnd` (`src/core/frame_router.cpp:19-22,30-33`), which is the **writer** side of the core-0→core-1 handshake. The core-1 reader (`dmxBufSnapshot` at `src/sys/tasks.cpp:100`) is seqlock-protected, so the cross-core write is safe.
- `mergeOutput()` is called synchronously within the router on core 0 (`src/core/frame_router.cpp:23,34`). The merge engine also reads the sender table which was just updated by `updateSender()` on the same core — no race.
- `viewOutput()` is called from `src/core/frame_router.cpp:24` — it reads `monitorOut` (set by `outputInitAll` on core 0 during `setup()` at `src/main.cpp:107`) — safe within the same task.
- The `maybeLog()` extern at `src/core/frame_router.cpp:10` is implemented in `stats.cpp` (`src/core/stats.cpp:14`) — called on core 0 only.

## 6. State Machine

No state machine — the router is a stateless dispatch function. Each call to `routeFrame()` or `routeFrameNzs()` processes one frame independently: find matching outputs → write buffer → merge → split → update stats.

## 7. Entry Points

| Entry point | Address | Caller |
|---|---|---|
| `routeFrame(int artUniverse, const uint8_t* data, uint16_t length, uint32_t senderIp, uint8_t proto, uint8_t priority)` | `src/core/frame_router.cpp:42` | `artHandlePacket()` (ArtDMX) (`src/net/artnet.cpp:189`); `commitArtSyncStaged()` (`src/net/artnet.cpp:244`); `handleSacnPacket()` direct path (`src/net/sacn.cpp:207-208`); `readSacn()` sync-loss commit (`src/net/sacn.cpp:244,271`) |
| `routeFrameNzs(int artUniverse, uint8_t* data, uint16_t length, uint8_t startCode, uint32_t senderIp, uint8_t priority)` | `src/core/frame_router.cpp:47` | `artHandlePacket()` (ArtNzs) (`src/net/artnet.cpp:233`) |
| `portAddress(const DmxOutput& o)` | `src/core/frame_router.h:8` (inline) | `mergeOutput()` (`src/core/merge_engine.cpp:25`); `routeFrameImpl()` (`src/core/frame_router.cpp:17,29`); `updateSender` in `commitArtSyncStaged` (`src/net/artnet.cpp:245`, `src/net/sacn.cpp:197`); `sacn.cpp:197,244,271` |

## 8. Data Flow

1. **Art-Net packet arrives** (core 0): `artRdmPollRx()` receives up to 8 packets into the ring (`src/net/artnet.cpp:68-81`).
2. **ArtPacket dispatched** (core 0): `artPktDispatchAll()` drains the ring to `artHandlePacket()` (`src/net/artnet.cpp:88-93`).
3. **Opcode check**: `artHandlePacket()` reads opcode from bytes 8-9 (`src/net/artnet.cpp:148`).
4. **ArtSync path**: if opcode is `ARTNET_OP_SYNC`, `syncMode` is cleared and `commitArtSyncStaged()` commits staged frames (`src/net/artnet.cpp:150-154`). If `syncMode` is active, incoming ArtDMX frames are staged to `dmxBufferState().staged[i]` instead of routed (`src/net/artnet.cpp:179-187`).
5. **ArtDMX path**: opcode `ARTNET_OP_DMX` → extract universe, length, priority (`src/net/artnet.cpp:171-178`) → call `routeFrame()` (`src/net/artnet.cpp:189`).
6. **ArtNzs path**: opcode `ARTNET_OP_NZS` → extract start code, universe, length, priority (`src/net/artnet.cpp:222-229`) → copy to stack frame → call `routeFrameNzs()` (`src/net/artnet.cpp:233`).
7. **sACN path** (core 0): `readSacnSocket()` receives up to 4 packets/socket into the ring (`src/net/sacn.cpp:211-226`). `readSacn()` drains the ring to `handleSacnPacket()` (`src/net/sacn.cpp:284-288`).
8. **sACN Stream Sync staging**: if output has `sacnSyncAddr[i] != 0`, frame is staged to `sacnStaged[i]` instead of routed (`src/net/sacn.cpp:194-206`).
9. **sACN direct path**: `handleSacnPacket()` calls `routeFrame()` with sACN universe converted to Art-Net port address (`src/net/sacn.cpp:188-208`).
10. **Frame routed**: `routeFrameImpl()` calls `updateSender()` to cache the source frame in the sender tracker (`src/core/frame_router.cpp:14`).
11. **Output matching**: iterates `MAX_OUTPUTS`; if `enabled` and `portAddress == universe`, writes the frame to `buffers[i]` via seqlock (`src/core/frame_router.cpp:16-22`).
12. **Merge**: calls `mergeOutput(i)` for each matched output (`src/core/frame_router.cpp:23`).
13. **Monitor log**: if `i == viewOutput()`, calls `maybeLog()` (`src/core/frame_router.cpp:24`).
14. **Universe split**: if `cfg.outputs[i].splitMask` is set, mirrors the frame to each bit-set output `j` (`src/core/frame_router.cpp:26-36`), writing to `buffers[j]` and calling `mergeOutput(j)`.
15. **Status update**: sets `stats().srcStatus = sourceStatus()` if any output matched (`src/core/frame_router.cpp:39`).

## 9. Protocol Layout

### ArtDMX packet (consumed by `routeFrame`)

| Offset | Size | Field | Source |
|---|---|---|---|
| 0-7 | 8 | "Art-Net" ID | `src/net/artnet.h:20` |
| 8-9 | 2 | Opcode `0x5000` (little-endian) | `src/net/artnet.cpp:148` |
| 10-11 | 2 | Protocol version | not inspected |
| 14-15 | 2 | Universe (16-bit LE) | `src/net/artnet.cpp:173` |
| 16-17 | 2 | Data length (big-endian) | `src/net/artnet.cpp:174` |
| 59 | 1 | Priority (0-255, or DEFAULT_PRIORITY) | `src/net/artnet.cpp:177` |
| 18.. | 512 | DMX data (start code + 512 slots) | `src/net/artnet.cpp:189` (`p + 18`) |

### ArtNzs packet (consumed by `routeFrameNzs`)

| Offset | Size | Field | Source |
|---|---|---|---|
| 0-7 | 8 | "Art-Net" ID | `src/net/artnet.h:20` |
| 8-9 | 2 | Opcode `0x5100` (little-endian) | `src/net/artnet.cpp:148` |
| 14-15 | 2 | Universe | `src/net/artnet.cpp:224` |
| 16-17 | 2 | Data length | `src/net/artnet.cpp:225` |
| 18 | 1 | Start code | `src/net/artnet.cpp:228` |
| 59 | 1 | Priority | `src/net/artnet.cpp:229` |
| 19.. | 511 | DMX slot data (512 minus start code byte) | `src/net/artnet.cpp:233` (`frame`) |

### sACN packet (consumed by `routeFrame`)

| Offset | Size | Field | Source |
|---|---|---|---|
| 4-15 | 12 | "ASC-E1.17" ID | `src/net/sacn.h:5` |
| 18-21 | 4 | Root vector `0x00000004` | `src/net/sacn.cpp:162-166` |
| 40-43 | 4 | Frame vector `0x00000002` (stream) | `src/net/sacn.cpp:168-171` |
| 108 | 1 | Priority | `src/net/sacn.cpp:184` |
| 113-114 | 2 | Universe (16-bit BE, 1-based) | `src/net/sacn.cpp:179-180` |
| 125 | 1 | Start code (must be `0x00`) | `src/net/sacn.cpp:182` |
| 126.. | 512 | DMX data | `src/net/sacn.cpp:188` (`p + SACN_DATA_OFF`) |

## 10. Config Integration

Reads:
- `cfg.outputs[i].enabled` (`include/config_schema.h:12`) — gates output participation (`src/core/frame_router.cpp:17,29`).
- `portAddress(cfg.outputs[i])` — derived from `net`, `subnet`, `universe` (`include/config_schema.h:13-15`) via `src/core/frame_router.h:9` (`src/core/frame_router.cpp:17,29`).
- `cfg.outputs[i].splitMask` (`include/config_schema.h:34`) — triggers universe splitting (`src/core/frame_router.cpp:26`).
- `cfg.outputs[i].sacnSync` — read indirectly via `dmxBufferState().sacnSyncAddr[i]` (set in `startSacn()` at `src/net/sacn.cpp:141-153`).

All fields are `CFG_LIVE` for signal-level settings (universe, splitMask) — changes apply on the next routed frame without a reboot (`include/config_types.h:22-25`). The router does not write config.

## 11. Lifecycle

- **Init**: No explicit init function. `g_outputs[]` and `outReady[]` are initialized in `outputInitAll()` (`src/core/output_init.cpp:35-111`), called from `setup()` at `src/main.cpp:107`.
- **Per-packet**: `routeFrame()` / `routeFrameNzs()` called on every ArtDMX, ArtNzs, or sACN stream packet (`src/net/artnet.cpp:189,233`; `src/net/sacn.cpp:208`).
- **Per-tick**: Not called from `dmxTxTask` — only from `netRxTask` on core 0 (`src/sys/tasks.cpp:149-151`). The core-1 task calls `mergeOutputTimed()` instead (`src/sys/tasks.cpp:135`).
- **Shutdown**: None.

## 12. Error Handling

- `routeFrameImpl()` returns `void`. If no output matches the incoming universe, `matched` stays `false` and nothing is written (`src/core/frame_router.cpp:15-39`). No logging on miss.
- Frame length is clamped to 512 in the network layer before the router is called (`src/net/artnet.cpp:175`, `src/core/frame_router.cpp:21` — `length > 512 ? 512 : length`).
- `viewOutput()` returns a valid output index even if `monitorOut` is disabled — it falls back to the first enabled output, or output 0 (`src/core/output_init.cpp:22-27`).
- `maybeLog()` is called only if `i == viewOutput()` (`src/core/frame_router.cpp:24`) — a 100 ms throttle is enforced inside `maybeLog()` (`src/core/stats.cpp:15-16`).

## 13. Memory Allocation

- `frame[DMX_PACKET_SIZE]` (513 bytes) is stack-allocated in `artHandlePacket()` for the ArtNzs path (`src/net/artnet.cpp:231`) — copied from the packet because `routeFrameNzs` takes a mutable `uint8_t*`.
- No heap allocation in the router. All output to the seqlock buffer is via `memcpy` into the pre-allocated `DmxBuffer.data` array.
- `contrib`-style index tracking does not exist in the router — it passes the full frame to `mergeOutput()`.

## 14. Timing

- `routeFrameImpl()` iterates up to `MAX_OUTPUTS` (4) outputs, each with a constant-time comparison (`src/core/frame_router.cpp:16-38`). O(4) per inbound packet.
- The split loop (`src/core/frame_router.cpp:26-36`) iterates another up to `MAX_OUTPUTS` (4) — worst case O(4) additional buffer writes + merges.
- Called from `netRxTask` on core 0, bounded to 8 Art-Net packets/tick (`src/net/artnet.cpp:68`) and 4 sACN packets/socket/tick (`src/net/sacn.cpp:215`). Net: up to 8 × 4 = 32 `routeFrame` calls per 2 ms tick.
- `mergeOutput()` within the router is synchronous — the core-1 seqlock reader may observe an in-progress write and retry, but this is bounded to 8 retries (`include/seqlock.h:22` / `src/core/dmx_buffer.cpp:8`).
- Best-effort on core 0 (priority 5); no hard deadline. The core-1 `dmxTxTask` (priority 19) is never blocked by the router.

## 15. Traceability

| Claim | Evidence |
|---|---|
| `portAddress` computes `(net << 8) | (subnet << 4) | universe` | `src/core/frame_router.h:8-10` |
| `routeFrameImpl` is the shared implementation | `src/core/frame_router.cpp:12-40` |
| `routeFrame` passes startCode=0 | `src/core/frame_router.cpp:44` |
| `routeFrameNzs` passes the caller's startCode | `src/core/frame_router.cpp:48-49` |
| `routeFrameNzs` hardcodes proto=0 (ArtNet) | `src/core/frame_router.cpp:49` |
| `updateSender` is called before output matching | `src/core/frame_router.cpp:14` |
| Buffer write wraps seqlock: begin → data[0] → data[1..] → end | `src/core/frame_router.cpp:19-22` |
| Length clamped to 512 on both data copies | `src/core/frame_router.cpp:21,32` |
| `mergeOutput(i)` called after direct write | `src/core/frame_router.cpp:23` |
| Monitor log via `viewOutput()` | `src/core/frame_router.cpp:24` |
| `splitMask` bit test: `cfg.outputs[i].splitMask & (1 << j)` | `src/core/frame_router.cpp:28` |
| Split output `j` must be enabled and not self | `src/core/frame_router.cpp:29` |
| Split also writes buffer + merges | `src/core/frame_router.cpp:30-34` |
| `stats().rxFrameCount[i]++` per routed frame | `src/core/frame_router.cpp:18` |
| `stats().srcStatus = sourceStatus()` on match | `src/core/frame_router.cpp:39` |
| ArtDMX length clamping at `length > 512` | `src/net/artnet.cpp:175` |
| sACN length clamping at `payloadLen > 512` | `src/net/sacn.cpp:186` |
| ArtNzs copies packet to stack frame | `src/net/artnet.cpp:231-232` |
| `maybeLog` extern declared | `src/core/frame_router.cpp:10` |
| `maybeLog` defined in stats.cpp with 100 ms throttle | `src/core/stats.cpp:14-16` |

## 16. Cross-References

- `[core-sender-tracker](./core-sender-tracker.md)` — `updateSender()` is called first in `routeFrameImpl()` (`src/core/frame_router.cpp:14`).
- `[core-merge-engine](./core-merge-engine.md)` — `mergeOutput()` is called after each buffer write (`src/core/frame_router.cpp:23,34`).
- `[core-dmx-buffer](./core-dmx-buffer.md)` — provides `dmxBufWriteBegin/WriteEnd` and `dmxBufferState()` (`src/core/dmx_buffer.cpp:5`).
- `[core-output-init](./core-output-init.md)` — provides `viewOutput()` for monitor logging (`src/core/output_init.cpp:22`).
- `[net-artnet-protocol](./net-artnet-protocol.md)` — primary caller: `artHandlePacket()` dispatches to `routeFrame` (`src/net/artnet.cpp:189`) and `routeFrameNzs` (`src/net/artnet.cpp:233`); also calls `commitArtSyncStaged` which routes staged frames (`src/net/artnet.cpp:238-248`).
- `[net-sacn-protocol](./net-sacn-protocol.md)` — `handleSacnPacket()` calls `routeFrame()` for direct sACN (`src/net/sacn.cpp:208`); `readSacn()` commits sync-staged frames via `routeFrame` (`src/net/sacn.cpp:244,271`).
- `core-stats` — `maybeLog()` definition and `StatsState` (`src/core/stats.cpp:14`, `src/core/stats.h:19-53`); `rxFrameCount/i` and `srcStatus` updated by the router.
- `sys-tasks` — `netRxTask` is the sole caller context (`src/sys/tasks.cpp:149-151`); `snapshotAndTransmit` reads the buffers the router writes (`src/sys/tasks.cpp:96-108`).
- `include-headers` — documents `include/config_schema.h` and `include/config_enums.h` (`docs/TECHNICAL_REFERENCE/include-headers.md`).

## 17. Limitations

- ArtSync and sACN Stream Sync staging is handled in the network layer (`artnet.cpp`/`sacn.cpp`), not in the router. The router only sees committed frames via `routeFrame`. This means the router cannot enforce a "hold all outputs until sync" policy — that is implicit in the staging arrays (`src/core/dmx_buffer.h:19-27`).
- The `splitMask` uses a simple `(1 << j)` bit test (`src/core/frame_router.cpp:28`) — with `MAX_OUTPUTS=4` this is fine, but if `MAX_OUTPUTS` were ever increased beyond 16, the `int`-sized `splitMask` would silently truncate (`include/config_schema.h:34`).
- `routeFrameNzs` hardcodes `proto=0` (ArtNet) at `src/core/frame_router.cpp:49` — sACN never has a non-zero start code, so this is correct, but the asymmetry is implicit.
- `maybeLog` is declared `extern` at `src/core/frame_router.cpp:10` rather than via a header include — a missing forward declaration would cause a linker error if `stats.cpp` changed its signature without updating the router.
- The router does not enforce ArtSync "hold" semantics (delaying non-synced frames) — it relies on `artHandlePacket` checking `g_artNet.syncMode` before calling `routeFrame` (`src/net/artnet.cpp:179-187`).

## 18. Open Questions

1. Not determinable from the inspected source code — whether `routeFrameNzs` is used for anything beyond ArtNzs opcode handling. It is called only from `artHandlePacket` at `src/net/artnet.cpp:233`; sACN packets with a non-zero start code are dropped (`src/net/sacn.cpp:182`).
2. Not determinable from the inspected source code — whether the `splitMask` mirroring path (`src/core/frame_router.cpp:26-36`) can cause a merge-order ambiguity if output `j` is both a direct match and a split target in the same frame iteration.
3. Not determinable from the inspected source code — whether there is a race between `artHandlePacket` staging a frame to `dmxBufferState().staged[i]` (`src/net/artnet.cpp:182`) and `commitArtSyncStaged` reading it (`src/net/artnet.cpp:242`), since staging and commit both run on core 0 but are not seqlock-protected (they are serialized by the single-threaded `netRxTask`, but this invariant is not documented in the source).

## 19. Testing

- `test/native/merge_test.cpp` — exercises the full router→sender→merge→buffer path indirectly: `updateSender` + `mergeOutput` + `dmxBufSnapshot` are all called in sequence (`test/native/merge_test.cpp:36-37,41`). The router's `routeFrame` is not called directly in the host test, but `merge_test.cpp` replicates its `updateSender` → `mergeOutput` → `dmxBufSnapshot` sequence.
- No dedicated `frame_router_test.cpp` exists — the router is a thin dispatch layer with no branch logic testable without the network layer mocks (ArtNet/sACN packet parsers).
- The split/universe-splitting path (`src/core/frame_router.cpp:26-36`) has no unit test coverage.
- The ArtNzs path (`routeFrameNzs`) has no host test coverage.

## 20. History

- Initial router design: simple `routeFrame(universe, data, length, ip, proto, priority)` dispatch from Art-Net and sACN (`src/core/frame_router.h:15`).
- `routeFrameNzs` added to handle ArtNzs (non-zero start code) opcode, preserving the start code in `buffers[i].data[0]` (`src/core/frame_router.cpp:47-50`, `src/core/frame_router.cpp:20`).
- `portAddress()` refactored to an inline function computing the 15-bit Art-Net address from (net, subnet, universe) (`src/core/frame_router.h:8-10`), replacing an earlier inline expression scattered across callers.
- `splitMask` field added to `DmxOutput` (`include/config_schema.h:34`) and universe-splitting logic added to `routeFrameImpl` (`src/core/frame_router.cpp:26-36`) to mirror a universe to multiple physical outputs.
- `stats().srcStatus = sourceStatus()` added (`src/core/frame_router.cpp:39`) to report conflict/merging state for the WebSocket status push.
