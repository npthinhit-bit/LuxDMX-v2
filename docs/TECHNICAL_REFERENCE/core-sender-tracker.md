# Sender Tracker — Technical Reference

Domain: core.sender-tracker

## 1. Domain Scope

Owns the table of inbound DMX data sources (senders) — one entry per active network sender, keyed by (IP, protocol). The module caches each sender's most recent frame, priority, universe, data length, frame rate, and last-activity timestamp. It also provides universe-mapping and source-status queries used by the merge engine and merge-loss detection.

The module does **not** parse network packets — it receives already-decoded frame data from `[core-frame-router](./core-frame-router.md)` (which is called by `[net-artnet-protocol](./net-artnet-protocol.md)` and `[net-sacn-protocol](./net-sacn-protocol.md)`). It does **not** perform merging — it hands the sender table to `[core-merge-engine](./core-merge-engine.md)`.

Consumers:
- `[core-frame-router](./core-frame-router.md):14` — calls `updateSender()` for every routed frame.
- `[core-merge-engine](./core-merge-engine.md):24,65,81` — reads sender entries for contributor filtering, priority comparison, and frame copying.
- `[core-merge-engine](./core-merge-engine.md):39` — reads `outSrcLost[i]` to set `stats().srcStatus`.
- `src/sys/tasks.cpp:110-118` — `flushArtSyncStaged()` is not here; the sender tracker is queried by `artnet.cpp:245` (via `updateSender` in `commitArtSyncStaged`).
- `src/net/artnet.cpp:186,245` and `src/net/sacn.cpp:203` — call `updateSender` directly for staged frames.

## 2. Layer Mapping

```
drv  →  cfg  →  core  →  net  →  app/sys
                    ↑      ↑
                    |      |
                    reads  |
                    cfg     |
                    (outputs)|
                            |
                            ↑
                    calls updateSender()
```

The sender tracker is a **core** layer module. It reads `cfg` (output enable/universe mapping) from the **cfg** layer and is called by both **net** layer consumers (artnet.cpp, sacn.cpp, frame_router.cpp) on core 0. The merge engine (core) reads the sender table concurrently on core 1 via `dmxTxTask`.

## 3. Source Files

| File | Role |
|---|---|
| `src/core/sender_tracker.h` | `Sender` struct (line 13), `SenderTracker` struct (line 26), `MAX_SENDERS`/`SOURCE_TIMEOUT_MS`/`DEFAULT_PRIORITY` defines (lines 9-11), function declarations (lines 31-38) |
| `src/core/sender_tracker.cpp` | `updateSender()` (line 16), `universeMapped()` (line 10), `activeSenderCount()` (line 58), `sourcesOnUniverse()` (line 66), `hasConflict()` (line 77), `isMerging()` (line 85), `sourceStatus()` (line 93) |
| `include/config_schema.h:9,47` | `MAX_OUTPUTS` define and `DmxOutput` struct (read for universe mapping) |
| `include/config_enums.h:5` | `MERGE_OFF` constant used in `hasConflict()`/`isMerging()` |

## 4. Data Structures

### `Sender` (`src/core/sender_tracker.h:13-24`)

| Field | Type | Description |
|---|---|---|
| `ip` | `uint32_t` (line 14) | Source IPv4 address. `0` = slot is empty. |
| `proto` | `uint8_t` (line 15) | Protocol byte: `0` = Art-Net, `1` = sACN (`src/core/sender_tracker.cpp:14,49`). |
| `lastMs` | `uint32_t` (line 16) | `millis()` timestamp of last frame from this sender. |
| `winMs` | `uint32_t` (line 17) | Window start for FPS calculation. |
| `winCnt` | `uint16_t` (line 18) | Frames received in current window. |
| `fps` | `float` (line 19) | Computed frame rate (updated every 1 s). |
| `universe` | `int16_t` (line 20) | 15-bit Art-Net port address or sACN universe (0-based). |
| `dataLen` | `uint16_t` (line 21) | Valid bytes in `data[]`, clamped to 512. |
| `priority` | `uint8_t` (line 22) | E1.31 priority (0-255). |
| `data` | `uint8_t[512]` (line 23) | Cached DMX slot data (indices 0-511 = slots 1-512). |

### `SenderTracker` (`src/core/sender_tracker.h:26-28`)

| Field | Type | Description |
|---|---|---|
| `senders` | `Sender[MAX_SENDERS]` (line 27) | Fixed table of sender slots. |

### Constants (`src/core/sender_tracker.h:6-11`)

| Define | Value | Description |
|---|---|---|
| `MAX_SENDERS` | 16 (configurable via `CONFIG_LUXDMX_MAX_SENDERS`) | Size of sender table. |
| `SOURCE_TIMEOUT_MS` | 2500 | Default source-loss timeout (2.5 s). |
| `DEFAULT_PRIORITY` | 100 | Priority used when the network source doesn't specify one. |

## 5. Concurrency

**Cross-core with implicit ordering — no mutex.**

- `updateSender()` is called on **core 0** (from `netRxTask` via `artnet.cpp`/`sacn.cpp`/`frame_router.cpp`) (`src/core/sender_tracker.cpp:7` — `g_senderTracker` is a static global accessed without a lock).
- `mergeOutput()` reads the sender table on **core 1** (from `dmxTxTask`) (`src/core/merge_engine.cpp:24`).
- There is **no explicit synchronization** between the writer (core 0 `updateSender`) and reader (core 1 `mergeOutput`). The ESP32's 32-bit aligned `uint32_t` writes are atomic, and the `Sender.data[512]` memcpy is the only multi-byte write. Because `dataLen` is updated *after* `data` is copied (`src/core/sender_tracker.cpp:46-47`), the reader always sees a consistent length paired with the data — the seqlock on the *output buffer* (not the sender table) is what prevents torn output, not a lock on this struct.
- `sourcesOnUniverse()`, `hasConflict()`, `isMerging()`, `sourceStatus()`, `activeSenderCount()` all read `millis()` (`Arduino.h`) — which is FreeRTOS-safe but not seqlock-protected.
- `g_senderTracker` is zero-initialized as a static at `src/core/sender_tracker.cpp:7` (C++ guarantees zero-init).

## 6. State Machine

No formal state machine. The sender table uses an implicit lifecycle:

- **Empty slot**: `ip == 0` (`src/core/sender_tracker.cpp:25`) — slot available for reuse.
- **Active sender**: `ip != 0`, `lastMs` within `SOURCE_TIMEOUT_MS` (2500 ms) of now (`src/core/sender_tracker.cpp:61-62`).
- **Stale sender**: `ip != 0`, `lastMs` older than timeout — still occupies a slot until evicted by the slot-reuse policy below.

Slot allocation order in `updateSender()` (`src/core/sender_tracker.cpp:22-35`):
1. Match existing (same `ip` + `proto`) — update in place.
2. Empty slot (`ip == 0`).
3. Slot whose universe is no longer mapped to any enabled output.
4. Oldest slot (`lastMs` minimum) — eviction fallback.

## 7. Entry Points

| Entry point | Address | Caller |
|---|---|---|
| `updateSender(...)` | `src/core/sender_tracker.cpp:16` | `routeFrameImpl()` (`src/core/frame_router.cpp:14`); `artHandlePacket()` staged path (`src/net/artnet.cpp:186`); `commitArtSyncStaged()` (`src/net/artnet.cpp:245`); `handleSacnPacket()` staged path (`src/net/sacn.cpp:203`); sync-loss commit (`src/net/sacn.cpp:244,271`) |
| `universeMapped(int)` | `src/core/sender_tracker.cpp:10` | `updateSender()` slot-eviction path (`src/core/sender_tracker.cpp:29`) |
| `activeSenderCount()` | `src/core/sender_tracker.cpp:58` | WebSocket push (`src/net/websocket.cpp` — address not inspected; see [Open Questions](#18-open-questions)) |
| `sourcesOnUniverse(int, uint32_t)` | `src/core/sender_tracker.cpp:66` | `hasConflict()` and `isMerging()` (`src/core/sender_tracker.cpp:80,88`) |
| `hasConflict()` | `src/core/sender_tracker.cpp:77` | WebSocket status push (`src/net/websocket.cpp` — see [Open Questions](#18-open-questions)) |
| `isMerging()` | `src/core/sender_tracker.cpp:85` | WebSocket status push (`src/net/websocket.cpp` — see [Open Questions](#18-open-questions)) |
| `sourceStatus()` | `src/core/sender_tracker.cpp:93` | `routeFrameImpl()` (`src/core/frame_router.cpp:39`) |

## 8. Data Flow

1. **Network packet decoded** (core 0): `artHandlePacket()` extracts universe, length, priority, data pointer (`src/net/artnet.cpp:171-190`) or `handleSacnPacket()` extracts the same from sACN (`src/net/sacn.cpp:157-209`).
2. **Frame routed** (core 0): `routeFrame()` or `routeFrameNzs()` → `routeFrameImpl()` (`src/core/frame_router.cpp:42-50`).
3. **Sender updated** (core 0): `routeFrameImpl()` calls `updateSender(senderIp, proto, universe, priority, data, length)` (`src/core/frame_router.cpp:14`).
4. **Slot allocation**: `updateSender()` searches for a matching (ip, proto) slot; if none, finds an empty or evictable slot (`src/core/sender_tracker.cpp:22-35`).
5. **Fresh sender reset**: if the slot was not a match, `data` is zeroed, `winMs`/`winCnt`/`fps` reset (`src/core/sender_tracker.cpp:38-42`).
6. **Frame cached**: `lastMs`, `universe`, `priority`, `dataLen` (clamped to 512 at `src/core/sender_tracker.cpp:46`) are written, then `data` is `memcpy`'d (`src/core/sender_tracker.cpp:43-47`).
7. **Stats updated**: `inFrameCnt[o]` incremented for each enabled output whose universe matches (`src/core/sender_tracker.cpp:48-49`).
8. **FPS window rolled**: every 1000 ms, `fps` is computed from `winCnt` and the window resets (`src/core/sender_tracker.cpp:50-55`).
9. **Source status computed**: `routeFrameImpl()` calls `sourceStatus()` → `hasConflict()` / `isMerging()` → `sourcesOnUniverse()` to set `stats().srcStatus` (`src/core/frame_router.cpp:39`, `src/core/sender_tracker.cpp:93-97`).
10. **Merge** (core 0 or 1): `routeFrameImpl()` calls `mergeOutput(i)` (`src/core/frame_router.cpp:23`) which reads the sender table on core 1 via `senderTracker().senders[i]` (`src/core/merge_engine.cpp:24,65,81`).

## 9. Protocol Layout

N/A (no wire protocol). The sender tracker consumes decoded DMX slot data (512 bytes max, start code stripped) and sender metadata (IP, protocol, priority, universe) that originate from the Art-Net or sACN wire packets. Wire protocol layouts are documented in `[net-artnet-protocol](./net-artnet-protocol.md)` and `[net-sacn-protocol](./net-sacn-protocol.md)`.

## 10. Config Integration

Reads:
- `cfg.outputs[o].enabled` (`include/config_schema.h:12`) — for universe mapping in `universeMapped()` (`src/core/sender_tracker.cpp:11`).
- `portAddress(cfg.outputs[o])` via `frame_router.h:8` — 15-bit address comparison in `universeMapped()` (`src/core/sender_tracker.cpp:12`).

Write: none. The sender tracker never writes config fields. All read fields (`enabled`, `net`, `subnet`, `universe`) are `CFG_LIVE` — the tracker picks up changes on the next `updateSender` or `sourcesOnUniverse` call without a reboot.

## 11. Lifecycle

- **Init**: `g_senderTracker` is zero-initialized as a static global at `src/core/sender_tracker.cpp:7`. All `Sender.ip` fields are `0` at boot, meaning all slots are empty.
- **Per-packet**: `updateSender()` is called on every routed frame (`src/core/frame_router.cpp:14`).
- **Per-tick**: `mergeOutputTimed()` on core 1 (`src/sys/tasks.cpp:135`) reads sender `lastMs` to detect timeouts.
- **Shutdown**: None. The table persists for the device lifetime; stale entries are evicted by the slot-reuse policy in `updateSender()` (`src/core/sender_tracker.cpp:27-34`).

## 12. Error Handling

- `updateSender()` has no return value (`void`). Slot allocation always succeeds — if all slots are occupied and none are evictable, the oldest slot is reused (`src/core/sender_tracker.cpp:30-34`).
- `sourcesOnUniverse()` returns `int` — 0 if no active sources, positive count otherwise (`src/core/sender_tracker.cpp:66-75`).
- `hasConflict()` and `isMerging()` return `bool` — `false` if no enabled outputs or all outputs in `MERGE_OFF` mode (`src/core/sender_tracker.cpp:77-91`).
- `sourceStatus()` returns `uint8_t`: `0` = normal, `1` = conflict, `2` = merging (`src/core/sender_tracker.cpp:93-97`).
- No logging is performed by this module — errors are silent and handled by the caller.

## 13. Memory Allocation

- `g_senderTracker` is a **static DRAM** allocation at `src/core/sender_tracker.cpp:7`. Size: `16 × sizeof(Sender)` = `16 × 585 = 9360` bytes (each `Sender` is `4 + 1 + 4 + 4 + 2 + 4 + 2 + 2 + 1 + 512 = 536` bytes with alignment padding → ~585 bytes with struct padding).
- `Sender.data[512]` at `src/core/sender_tracker.h:23` is the dominant per-sender cost.
- No heap allocation. All sender data is statically allocated.

## 14. Timing

- `updateSender()` scans up to `MAX_SENDERS` (16) slots for a matching (ip, proto) pair — O(16) per routed packet (`src/core/sender_tracker.cpp:20-21`).
- If no match and no empty slot, a second O(16) scan finds an evictee (`src/core/sender_tracker.cpp:24-25`), and a third O(16) scan finds the oldest (`src/core/sender_tracker.cpp:30-34`). Worst case: 3 × 16 = 48 comparisons per new sender.
- `sourcesOnUniverse()` scans 16 senders — O(16) per call (`src/core/sender_tracker.cpp:69-74`).
- `hasConflict()`/`isMerging()` each iterate `MAX_OUTPUTS` (4) and call `sourcesOnUniverse()` — O(4 × 16) = 64 comparisons per call (`src/core/sender_tracker.cpp:78-90`).
- `sourceStatus()` chains `hasConflict()` + `isMerging()` — worst case 128 comparisons (`src/core/sender_tracker.cpp:93-96`).
- `activeSenderCount()` scans 16 senders with a 5000 ms window (`src/core/sender_tracker.cpp:61-62`).
- FPS window rollover every 1000 ms (`src/core/sender_tracker.cpp:51`).
- `SOURCE_TIMEOUT_MS` = 2500 ms (`src/core/sender_tracker.h:10`).

## 15. Traceability

| Claim | Evidence |
|---|---|
| `MAX_SENDERS` defaults to 16 | `src/core/sender_tracker.h:7-9` |
| `SOURCE_TIMEOUT_MS` is 2500 ms | `src/core/sender_tracker.h:10` |
| `DEFAULT_PRIORITY` is 100 | `src/core/sender_tracker.h:11` |
| `Sender` struct layout | `src/core/sender_tracker.h:13-24` |
| `SenderTracker` holds fixed array of `Sender` | `src/core/sender_tracker.h:26-28` |
| `g_senderTracker` is zero-initialized static | `src/core/sender_tracker.cpp:7` |
| `updateSender` signature: ip, proto, universe, priority, data, length | `src/core/sender_tracker.h:32-33` |
| Slot matching: same (ip, proto) | `src/core/sender_tracker.cpp:20-21` |
| Empty-slot detection: `ip == 0` | `src/core/sender_tracker.cpp:25` |
| Eviction: universe no longer mapped | `src/core/sender_tracker.cpp:27-29` |
| Eviction fallback: oldest by `lastMs` | `src/core/sender_tracker.cpp:30-34` |
| Fresh sender: zero data, reset window | `src/core/sender_tracker.cpp:37-42` |
| `dataLen` clamped to 512 | `src/core/sender_tracker.cpp:46` |
| `inFrameCnt` incremented per matching output | `src/core/sender_tracker.cpp:48-49` |
| FPS computed every 1000 ms | `src/core/sender_tracker.cpp:50-55` |
| `activeSenderCount` uses 5000 ms window | `src/core/sender_tracker.cpp:62` |
| `sourcesOnUniverse` filters by ip + universe + window | `src/core/sender_tracker.cpp:69-73` |
| `hasConflict` checks `MERGE_OFF` outputs only | `src/core/sender_tracker.cpp:78-80` |
| `isMerging` checks non-`MERGE_OFF` outputs | `src/core/sender_tracker.cpp:86-88` |
| `sourceStatus`: 0=normal, 1=conflict, 2=merging | `src/core/sender_tracker.cpp:93-97` |
| No explicit lock on sender table | `src/core/sender_tracker.cpp:7,36` (static global, no mutex) |
| `proto` byte: 0=ArtNet, 1=sACN | `src/core/sender_tracker.cpp:14` (routeFrame passes 0), `src/core/frame_router.cpp:49` (routeFrameNzs passes 0), `src/net/sacn.cpp:208` (routeFrame passes 1) |

## 16. Cross-References

- `[core-frame-router](./core-frame-router.md)` — calls `updateSender()` as the primary entry point (`src/core/frame_router.cpp:14`).
- `[core-merge-engine](./core-merge-engine.md)` — reads `senders[i]` for contributor selection, priority comparison, and frame copying (`src/core/merge_engine.cpp:24,65,67,81,97,111,117`).
- `[core-dmx-buffer](./core-dmx-buffer.md)` — not directly, but the sender's 512-byte `data` field is the source for the seqlock-protected output buffer writes.
- `[net-artnet-protocol](./net-artnet-protocol.md)` — calls `updateSender` directly for staged frames (`src/net/artnet.cpp:186,245`).
- `[net-sacn-protocol](./net-sacn-protocol.md)` — calls `updateSender` for staged frames (`src/net/sacn.cpp:203,244,271`).
- `core-stats` — `inFrameCnt` is incremented here (`src/core/sender_tracker.cpp:49`), and `srcStatus` is set from `sourceStatus()` (`src/core/frame_router.cpp:39`).
- `sys-tasks` — `mergeOutputTimed()` reads sender `lastMs` for timeout detection (`src/sys/tasks.cpp:128-129`).
- `include-headers` — documents `include/config_schema.h` and `include/config_enums.h` (`docs/TECHNICAL_REFERENCE/include-headers.md`).

## 17. Limitations

- No explicit synchronization between the core-0 writer (`updateSender`) and core-1 reader (`mergeOutput`) — relies on atomic 32-bit writes and the `dataLen`-after-`data` write ordering (`src/core/sender_tracker.cpp:47-48`). A torn 512-byte `memcpy` of `Sender.data` is theoretically possible if the core-1 reader reads mid-write of a large frame.
- `Sender.data` is fixed at 512 bytes (`src/core/sender_tracker.h:23`) — supports full DMX512 but no larger payloads.
- The slot-eviction policy (`src/core/sender_tracker.cpp:27-34`) can evict a still-active sender if its universe is unmapped, even if it is within the 5 s window — this is by design (universe remapping) but means rapid sender turnover on a shared universe can churn the table.
- `activeSenderCount()` uses a hardcoded 5000 ms window (`src/core/sender_tracker.cpp:62`), not `SOURCE_TIMEOUT_MS` (2500) — the two timeouts are decoupled and not documented as such.

## 18. Open Questions

1. Not determinable from the inspected source code — which WebSocket or web-server function calls `activeSenderCount()`, `hasConflict()`, `isMerging()`, and `sourceStatus()` for status reporting. These are declared at `src/core/sender_tracker.h:34-38` but their callers outside the inspected files are not identified.
2. Not determinable from the inspected source code — whether the sender table is ever cleared/reset on a full network disconnect or config change, or whether eviction relies solely on the 5000 ms window expiry in `activeSenderCount`.
3. Not determinable from the inspected source code — whether there is a race between `updateSender`'s `dataLen` write and the core-1 reader's `dataLen` read that could expose a partially-updated `data` buffer. The write order (data first, then dataLen) at `src/core/sender_tracker.cpp:47-48` suggests intentional design, but no comment documents this invariant.

## 19. Testing

- `test/native/merge_test.cpp` — exercises `updateSender()` extensively: HTP merge setup (`test/native/merge_test.cpp:36-37`), OFF/last-frame-wins (`test/native/merge_test.cpp:54-55`), cross-universe isolation (`test/native/merge_test.cpp:86`), LTP-Takeover (`test/native/merge_test.cpp:106-109`), priority merge (`test/native/merge_test.cpp:128-130`), failsafe timeout (`test/native/merge_test.cpp:148,164`).
- `merge_test.cpp` also manipulates `senderTracker().senders[0].lastMs` directly to simulate timing (`test/native/merge_test.cpp:108`).
- No dedicated `sender_tracker_test.cpp` exists — the sender tracker is validated only through the merge engine tests.
- `activeSenderCount()`, `hasConflict()`, `isMerging()`, `sourceStatus()`, `universeMapped()` have no unit test coverage in the inspected test files.

## 20. History

- Initial sender table design: fixed 16-slot array with (ip, proto) keying (`src/core/sender_tracker.h:9,13-14`).
- `proto` byte added (0=ArtNet, 1=sACN) to allow coexisting Art-Net and sACN sources on the same universe (`src/core/sender_tracker.h:15`, `src/core/sender_tracker.cpp:49`).
- Slot eviction policy added: empty slot → unmapped universe → oldest (`src/core/sender_tracker.cpp:22-34`) to handle sender churn without a fixed timeout-based clear.
- FPS window calculation added (`src/core/sender_tracker.cpp:50-55`) for WebSocket status reporting.
- `sourceStatus()` added to expose conflict/merging state as a 0/1/2 code for the web UI (`src/core/sender_tracker.cpp:93-97`).
- 5000 ms window in `activeSenderCount()` decoupled from `SOURCE_TIMEOUT_MS` (2500 ms) at `src/core/sender_tracker.h:10,62` — used for WebSocket "active sender" display rather than merge logic.
