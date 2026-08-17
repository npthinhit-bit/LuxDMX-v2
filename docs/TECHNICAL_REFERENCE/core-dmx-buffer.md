# DMX Buffer — Technical Reference

Domain: core.dmx-buffer

## 1. Domain Scope

Owns the per-output DMX frame storage that sits between the network receive path (core 0) and the RMT transmit path (core 1). The module provides:

- A seqlock-protected 513-byte DMX frame buffer per output (`DmxBuffer`).
- Write-side helpers that bracket a `memcpy` with a 32-bit seqlock ticket bump.
- A read-side snapshot function that retries on torn writes and counts skips.
- ArtSync staging arrays and sACN Stream-Sync staging arrays, both per-output.
- A generic single-writer/single-reader `SeqLock` primitive in `include/seqlock.h` used by this module and by the seqlock unit test.

Delegates nothing — the module is a pure data store. It does not read packets, merge sources, or drive hardware.

Consumers:
- `src/core/frame_router.cpp:19-22` — writes frames into `DmxBufferState::buffers[i].data` via `dmxBufWriteBegin/WriteEnd`.
- `src/core/merge_engine.cpp:38-39, 70-71, 87-88, 102-103, 116-117` — writes merged frames via `dmxBufWriteBegin/WriteEnd`.
- `src/net/artnet.cpp:182-184, 238-248` — writes to `staged[]` (ArtSync) and commits to `buffers[]` via `commitArtSyncStaged`.
- `src/net/sacn.cpp:199-201, 244, 271` — writes to `sacnStaged[]` (Stream Sync) and commits via `routeFrame`.
- `src/sys/tasks.cpp:100` — reads frames via `dmxBufSnapshot`.
- `src/sys/tasks.cpp:98` — reads `txStyle`/`outSrcLost` to gate transmission.

## 2. Layer Mapping

```
drv  →  cfg  →  core  →  net  →  app/sys
                ↑     ↖      ↖      ↖
                |      \      \      \
                |       consumes     consumes
                |       (writes)      (reads via
                |                     snapshotAndTransmit)
```

The DMX buffer is a **core** layer primitive. It is written by **net** layer consumers (artnet.cpp, sacn.cpp, frame_router.cpp) on core 0 and read by the **sys** layer transmit task (tasks.cpp) on core 1.

## 3. Source Files

| File | Role |
|---|---|
| `src/core/dmx_buffer.h` | `DmxBuffer` struct, `DmxBufferState` struct, `DMX_OUTPUT_COUNT` define, `dmxBufferState()` accessor, inline `dmxBufWriteBegin/end/Set` writers |
| `src/core/dmx_buffer.cpp` | `dmxBufSnapshot` reader implementation, `g_dmxBufState` static instance |
| `include/seqlock.h` | Generic `SeqLock` struct with `writeBegin/writeEnd/snapshot` inline methods |
| `include/rdm_types.h:33` | `DMX_PACKET_SIZE` (513) — the frame size constant used by `DmxBuffer::data` |

## 4. Data Structures

### `SeqLock` (`include/seqlock.h:11-32`)

| Field | Offset/Type | Description |
|---|---|---|
| `seq` | `volatile uint32_t` (line 12) | 32-bit ticket. Odd = writer mid-update; even = stable. |

Methods:
- `writeBegin()` (line 14): `seq++`; `__sync_synchronize()` — bumps to odd, fences before write.
- `writeEnd()` (line 15): `__sync_synchronize()`; `seq++` — fences after write, bumps back to even.
- `snapshot(src, dst, n)` (line 21-31): up to 8 retries; reads `seq`, skips if odd, `memcpy`, re-reads `seq`, returns `true` if unchanged. Returns `false` after 8 failed retries.

### `DmxBuffer` (`src/core/dmx_buffer.h:11-14`)

| Field | Type | Description |
|---|---|---|
| `data` | `uint8_t[DMX_PACKET_SIZE]` (513 bytes, line 12) | Start code at index 0, 512 slots at indices 1-512. |
| `seq` | `volatile uint32_t` (line 13) | Seqlock ticket for this buffer. |

### `DmxBufferState` (`src/core/dmx_buffer.h:16-28`)

| Field | Type | Description |
|---|---|---|
| `buffers` | `DmxBuffer[DMX_OUTPUT_COUNT]` (line 17) | Main seqlock-protected frame buffers; writer core 0, reader core 1. |
| `tornSkips` | `uint32_t` (line 18) | Counter incremented when `dmxBufSnapshot` fails all 8 retries. |
| `staged` | `uint8_t[DMX_OUTPUT_COUNT][DMX_PACKET_SIZE]` (line 20) | ArtSync staging: frames held until ArtSync arrives. |
| `stagedValid` | `bool[DMX_OUTPUT_COUNT]` (line 21) | Per-output flag: is `staged[i]` dirty? |
| `stagedLen` | `uint16_t[DMX_OUTPUT_COUNT]` (line 22) | Valid byte count in `staged[i]`. |
| `sacnStaged` | `uint8_t[DMX_OUTPUT_COUNT][DMX_PACKET_SIZE]` (line 24) | sACN Stream Sync staging. |
| `sacnStagedValid` | `bool[DMX_OUTPUT_COUNT]` (line 25) | Per-output sACN sync staging valid flag. |
| `sacnStagedLen` | `uint16_t[DMX_OUTPUT_COUNT]` (line 26) | Valid byte count in `sacnStaged[i]`. |
| `sacnSyncAddr` | `uint16_t[DMX_OUTPUT_COUNT]` (line 27) | Per-output sACN sync universe address (0 = none). |

### `DMX_OUTPUT_COUNT` (`src/core/dmx_buffer.h:7`)

`#define DMX_OUTPUT_COUNT MAX_OUTPUTS` — resolves to `CONFIG_LUXDMX_MAX_OUTPUTS` (default 4) via `include/config_schema.h:9`.

## 5. Concurrency

**Dual-core, single-writer/single-reader seqlock.**

- **Writer** (core 0, `netRxTask`, priority 5): calls `dmxBufWriteBegin(i)` → writes data → `dmxBufWriteEnd(i)`. The seqlock makes the 513-byte `memcpy` atomic from the reader's perspective. The writer is never blocked by the reader (`include/seqlock.h:6-10`).
- **Reader** (core 1, `dmxTxTask`, priority 19): calls `dmxBufSnapshot(i, frame)` at `src/sys/tasks.cpp:100`. Retries up to 8 times; if all fail, increments `tornSkips` and returns `false`, causing the transmit task to hold the previous frame (`src/sys/tasks.cpp:100`).
- **Memory fence**: `__sync_synchronize()` is used after every seqlock read and before every seqlock re-read (`src/core/dmx_buffer.cpp:11-14`, `src/core/dmx_buffer.cpp:11-13`), matching the generic `SeqLock` pattern at `include/seqlock.h:14-15,21-31`.
- **Staged arrays**: `staged[]`, `sacnStaged[]` are written by core 0 (artnet.cpp, sacn.cpp) and read by `commitArtSyncStaged()` (artnet.cpp:238) and `readSacn()` (sacn.cpp:237-250), both also on core 0. No cross-core seqlock is needed for these — they are same-core SPSC via the task's own serialization.

## 6. State Machine

No state machine — the buffer module is stateless from a control-flow perspective. The `stagedValid[i]` and `sacnStagedValid[i]` flags act as binary staging state (valid/invalid), but there is no transition graph.

## 7. Entry Points

Called by the task loop or network dispatch:

| Entry point | Address | Caller |
|---|---|---|
| `dmxBufSnapshot(int, uint8_t*)` | `src/core/dmx_buffer.cpp:7` | `snapshotAndTransmit()` at `src/sys/tasks.cpp:100` |
| `dmxBufWriteBegin(int)` | `src/core/dmx_buffer.h:32` (inline) | `frame_router.cpp:19`, `merge_engine.cpp:38,70,87,102,116`, `artnet.cpp:241` |
| `dmxBufWriteEnd(int)` | `src/core/dmx_buffer.h:33` (inline) | Same callers as `dmxBufWriteBegin` |
| `dmxBufWriteEndSet(int, int, uint8_t)` | `src/core/dmx_buffer.h:36` (inline) | WebSocket manual control path (not in inspected source — see [Open Questions](#18-open-questions)) |

## 8. Data Flow

1. **Network packet received** on core 0: `artRdmPollRx()` reads up to 8 Art-Net packets into the ring (`src/net/artnet.cpp:68-81`), or `readSacnSocket()` reads up to 4 sACN packets (`src/net/sacn.cpp:215-226`).
2. **Packet dispatched** on core 0: `artPktDispatchAll()` drains the Art-Net ring (`src/net/artnet.cpp:88-93`), calling `artHandlePacket()` which calls `routeFrame()` or `routeFrameNzs()` (`src/net/artnet.cpp:189,233`). For sACN, `readSacn()` drains the ring and calls `handleSacnPacket()` (`src/net/sacn.cpp:286-288`).
3. **Frame routed** to output buffer: `routeFrameImpl()` iterates enabled outputs (`src/core/frame_router.cpp:16-38`). For each match it calls `dmxBufWriteBegin(i)` → writes `data[0]` (start code) and `data[1..512]` → `dmxBufWriteEnd(i)` (`src/core/frame_router.cpp:19-22`).
4. **Merge** (optional): `routeFrameImpl()` calls `mergeOutput(i)` (`src/core/frame_router.cpp:23`) which may overwrite the buffer with a merged frame from multiple senders.
5. **ArtSync staging** (alternate path): when `syncMode` is active, `artHandlePacket()` copies the frame to `dmxBufferState().staged[i]` instead of `buffers[i]` (`src/net/artnet.cpp:179-187`). On ArtSync receipt or timeout, `commitArtSyncStaged()` copies `staged[i]` → `buffers[i].data[1..512]` via the seqlock (`src/net/artnet.cpp:238-248`).
6. **sACN Stream Sync staging** (alternate path): `handleSacnPacket()` copies to `sacnStaged[i]` when `sacnSyncAddr[i] != 0` (`src/net/sacn.cpp:197-205`). Committed on sync packet or 2500 ms timeout in `readSacn()` (`src/net/sacn.cpp:237-250`).
7. **Snapshot on core 1**: `dmxTxTask` → `dmxFrameTick()` → `snapshotAndTransmit()` → `dmxBufSnapshot(outIdx, frame)` (`src/sys/tasks.cpp:96-108`). If the snapshot returns `false` (8 torn retries), the previous frame is held and no transmit occurs (`src/sys/tasks.cpp:100`).
8. **RMT transmit**: if `rmtDmxIdle(rd)` returns true, `rmtDmxKick(rd, frame, DMX_PACKET_SIZE)` starts the hardware transmit (`src/sys/tasks.cpp:102-106`, `src/drv/dmx_rmt.h:172-179`).

## 9. Protocol Layout

N/A (no wire protocol). The buffer module stores decoded DMX frame payloads (513 bytes: 1 start code + 512 slots) that originate from Art-Net or sACN wire packets. The wire protocol layouts are documented in `[net-artnet-protocol](./net-artnet-protocol.md)` and `[net-sacn-protocol](./net-sacn-protocol.md)`.

## 10. Config Integration

Reads:
- `cfg.outputs[i].enabled` — gates whether the buffer is used (`src/core/frame_router.cpp:17`)
- `cfg.outputs[i].splitMask` — triggers universe splitting to mirrored outputs (`src/core/frame_router.cpp:26-36`)
- `cfg.outputs[i].lossMode` — read by `mergeOutput()` on source loss (`src/core/merge_engine.cpp:36`)
- `cfg.outputs[i].lossPreset` — read by `mergeOutput()` on source loss (`src/core/merge_engine.cpp:46`)
- `cfg.outputs[i].mergeMode` — read by `mergeOutput()` (`src/core/merge_engine.cpp:62,77,93,108`)

Write: none. The buffer module never writes config fields back.

All fields are `CFG_LIVE` for signal-level settings (universe, merge mode, loss mode, split mask) and `CFG_REBOOT` for pin/GPIO settings (`include/config_types.h:16-22`, `include/config_enums.h:5-6`). The buffer module itself does not trigger live-apply or reboot logic — it simply reads `cfg` at call time.

## 11. Lifecycle

- **Init**: No explicit init function. `g_dmxBufState` is zero-initialized as a static at `src/core/dmx_buffer.cpp:4` (C++ guarantees zero-init for static-duration objects). The seqlock `seq` fields start at 0 (even = stable).
- **Runtime**: Read/written on every DMX frame tick (1 ms) and on every incoming network packet.
- **Shutdown**: None. The module has no shutdown hook — buffers persist in DRAM for the device lifetime.

## 12. Error Handling

- `dmxBufSnapshot()` returns `bool`: `true` on consistent snapshot, `false` after 8 failed retries (`src/core/dmx_buffer.cpp:7-17`). On failure, `tornSkips++` is incremented (`src/core/dmx_buffer.cpp:16`) and no data is written to the output pointer.
- `dmxBufWriteEndSet()` silently drops out-of-range channel writes: `if (ch >= 1 && ch < DMX_PACKET_SIZE)` (`src/core/dmx_buffer.h:37`).
- No logging is performed by this module on torn reads — the caller (`src/sys/tasks.cpp:100`) simply skips the transmit.

## 13. Memory Allocation

- `g_dmxBufState` is a **static DRAM** allocation of `sizeof(DmxBufferState)` at `src/core/dmx_buffer.cpp:4`.
- Size calculation: `DMX_OUTPUT_COUNT` (4) × (`DmxBuffer` 513 bytes + `staged` 513 + `stagedLen` 2 + `stagedValid` 1 + `sacnStaged` 513 + `sacnStagedLen` 2 + `sacnStagedValid` 1 + `sacnSyncAddr` 2) ≈ 4 × 1032 ≈ 4128 bytes + 4 (tornSkips) = ~4.1 KB of statically-allocated DRAM.
- `RmtDmx::sym` (the RMT symbol buffer) is separately allocated via `heap_caps_malloc` with `MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA` in `src/drv/dmx_rmt.h:112-113`, but that belongs to the driver module, not this buffer.
- The `SeqLock::snapshot` local `s1` and the `frame[DMX_PACKET_SIZE]` temp in `snapshotAndTransmit` are stack/DRAM-temporary (`src/sys/tasks.cpp:99`).

## 14. Timing

- **Write side** (core 0): O(1) seqlock bump + O(513) memcpy per frame. Bounded by network packet arrival rate (Art-Net: up to 8 packets/tick at `src/net/artnet.cpp:68`; sACN: up to 4/socket at `src/net/sacn.cpp:215`).
- **Read side** (core 1): up to 8 seqlock retry iterations, each doing a full 513-byte `memcpy` (`src/core/dmx_buffer.cpp:8-14`). Worst case: 8 × 513 bytes copied = ~4 KB of memory fence + memcpy per snapshot call. In practice, retries are rare — the writer (core 0) and reader (core 1) rarely race because the 1 ms DMX tick is much slower than the microsecond-scale write.
- **Transmit tick**: `dmxTxTask` runs every 1 ms via `vTaskDelayUntil(&xLastWakeTime, 1)` (`src/sys/tasks.cpp:142`). Each tick calls `snapshotAndTransmit` for every enabled+ready output (`src/sys/tasks.cpp:127-133`).
- **Torn-read tolerance**: a failed snapshot holds the previous frame for one tick (1 ms) — benign, as stated in `include/seqlock.h:19-20`.

## 15. Traceability

| Claim | Evidence |
|---|---|
| `DMX_PACKET_SIZE` is 513 | `include/rdm_types.h:33` |
| `DMX_OUTPUT_COUNT` resolves to `MAX_OUTPUTS` | `src/core/dmx_buffer.h:7` → `include/config_schema.h:9` |
| `DmxBuffer` has 513-byte data array + seqlock field | `src/core/dmx_buffer.h:11-14` |
| `DmxBufferState` holds buffers, staging arrays, tornSkips counter | `src/core/dmx_buffer.h:16-28` |
| Writer bumps seq to odd, fences, writes, fences, bumps to even | `src/core/dmx_buffer.h:32-33` |
| `dmxBufSnapshot` retries 8 times, copies 513 bytes, checks seq stability | `src/core/dmx_buffer.cpp:7-17` |
| `tornSkips` is incremented on persistent tear | `src/core/dmx_buffer.cpp:16` |
| Generic `SeqLock` uses same odd/even ticket + 8-retry pattern | `include/seqlock.h:11-32` |
| Writer runs on core 0, reader on core 1 | `src/core/dmx_buffer.h:9-10` (comment); `src/sys/tasks.cpp:83,142` (task pinning) |
| `snapshotAndTransmit` is the reader call site | `src/sys/tasks.cpp:96-108` |
| ArtSync staging writes to `staged[i]` | `src/net/artnet.cpp:182-184` |
| ArtSync commit copies `staged[i]` → `buffers[i]` | `src/net/artnet.cpp:238-248` |
| sACN Stream Sync staging writes to `sacnStaged[i]` | `src/net/sacn.cpp:199-201` |
| sACN sync commit routes from `sacnStaged[i]` | `src/net/sacn.cpp:244,271` |
| RMT transmit starts after idle check on snapshot success | `src/sys/tasks.cpp:100-106` |
| `g_dmxBufState` is zero-initialized static | `src/core/dmx_buffer.cpp:4` |

## 16. Cross-References

- `[net-artnet-protocol](./net-artnet-protocol.md)` — produces frames staged via `staged[]` (`src/net/artnet.cpp:179-187`) and consumed by `commitArtSyncStaged` (`src/net/artnet.cpp:238-248`).
- `[net-sacn-protocol](./net-sacn-protocol.md)` — produces frames staged via `sacnStaged[]` (`src/net/sacn.cpp:197-205`) and committed in `readSacn` (`src/net/sacn.cpp:237-250`).
- `[core-frame-router](./core-frame-router.md)` — writes live frames to `buffers[i]` via `dmxBufWriteBegin/WriteEnd` (`src/core/frame_router.cpp:19-22`).
- `[core-merge-engine](./core-merge-engine.md)` — overwrites `buffers[i].data[1..512]` with merged data via seqlock (`src/core/merge_engine.cpp:38-39, 70-71, 87-88, 102-103, 116-117`).
- `sys-tasks` — reads via `dmxBufSnapshot` in `snapshotAndTransmit` (`src/sys/tasks.cpp:96-108`); not yet a separate module file (see [Open Questions](#18-open-questions)).
- `include-headers` — documents `include/seqlock.h` and `include/rdm_types.h` shared types (`docs/TECHNICAL_REFERENCE/include-headers.md`).

## 17. Limitations

- The buffer uses a fixed `DMX_OUTPUT_COUNT` (default 4) — `include/config_schema.h:93-94` warns that ESP32-S3 has only 4 RMT TX channels, so >4 outputs compile but cannot transmit.
- `tornSkips` is a `uint32_t` with no reset path — it overflows after ~4 billion torn reads (not realistically reachable).
- The seqlock retry count is hardcoded to 8 (`src/core/dmx_buffer.cpp:8`); if the core-0 writer is starved for >8 snapshot-attempt durations, frames are held without logging.
- `dmxBufWriteEndSet` (WebSocket manual control) is declared inline in the header but its caller is not visible in the inspected source (`src/core/dmx_buffer.h:36-39`).

## 18. Open Questions

1. Not determinable from the inspected source code — which function calls `dmxBufWriteEndSet` for WebSocket manual channel control. The function is declared at `src/core/dmx_buffer.h:36` but no caller is found in the inspected files.
2. Not determinable from the inspected source code — whether `flushArtSyncStaged()` in `src/sys/tasks.cpp:110-118` is the only commit trigger, or if `commitArtSyncStaged` is also called from a WebSocket or serial path (it is called from `artHandlePacket` at `src/net/artnet.cpp:153` on ArtSync receipt and at `src/net/artnet.cpp:161` as timeout fallback).
3. Not determinable from the inspected source code — whether `sacnSyncAddr[i]` (the sync universe) is set from config or only from runtime `cfg.outputs[i].sacnSync` via `startSacn()` at `src/net/sacn.cpp:141-153`.

## 19. Testing

- `test/native/seqlock_test.cpp` — tests the generic `SeqLock` at `include/seqlock.h:21` (clean snapshot, stable write, 100 write-read cycles). Run via `python3 test/native/test_native.py seqlock_test` (`CLAUDE.md` build section).
- `test/native/merge_test.cpp` — exercises `dmxBufSnapshot` as part of merge verification (`test/native/merge_test.cpp:41,59,74,...`), confirming snapshot returns `true` in the expected test conditions.
- No dedicated `dmx_buffer_test.cpp` exists — the buffer's seqlock semantics are validated indirectly through `seqlock_test.cpp` (generic) and `merge_test.cpp` (via `dmxBufSnapshot`).
- The `frame[513]` stack buffer in `snapshotAndTransmit` (`src/sys/tasks.cpp:99`) is not separately unit-tested — it is exercised only on hardware during the soak-test monitor workflow (`src/sys/soak_monitor.cpp`).

## 20. History

- Issue #64: DMX transmission moved from UART+GPTimer ISR to RMT peripheral to fix break corruption under core-0 network DMA contention. The seqlock buffer was added as part of the same issue (`include/seqlock.h:5`).
- Initial seqlock design: single-writer (core 0 `netRxTask`) / single-reader (core 1 `dmxTxTask`), with `__sync_synchronize()` full memory barriers (`include/seqlock.h:9-10`).
- ArtSync staging fields (`staged`, `stagedValid`, `stagedLen`) added to `DmxBufferState` to support synchronized frame commit (`src/core/dmx_buffer.h:19-22`).
- sACN Stream Sync staging fields (`sacnStaged`, `sacnStagedValid`, `sacnStagedLen`, `sacnSyncAddr`) added to `DmxBufferState` for E1.31 Stream Sync support (`src/core/dmx_buffer.h:23-27`).
