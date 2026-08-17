# Sender Tracker - System Specification

## 1. Module Overview

**Module ID:** core.sender-tracker
**Domain:** Source tracking for inbound DMX network data
**Layer:** core (reads cfg, consumed by net/frame-router/merge on core 0; read by merge on core 1)

Maintains a table of up to 16 active network DMX sources (senders), each keyed by a (source IP, protocol) pair. For each sender, caches the most recent frame data (512 bytes), universe, E1.31 priority, frame rate, and activity timestamp. Provides universe-mapping and source-status queries consumed by the merge engine and frame router to drive merge decisions, source-loss detection, and web-status reporting.

The module does **not** parse network packets — it receives already-decoded frame data from the Frame Router (which is invoked by Art-Net and sACN protocol layers). It does **not** perform merging — it hands the sender table to the Merge Engine.

## 2. External Interfaces

### Entry Points

| Function | Caller | Trigger |
|---|---|---|
| `updateSender(ip, proto, universe, priority, data, length)` | Frame Router (primary), Art-Net protocol (staged ArtSync commit), sACN protocol (staged frame + sync-loss commit) | Every routed network frame |
| `universeMapped(universe)` | `updateSender()` (slot-eviction path) | During sender slot allocation |
| `activeSenderCount()` | WebSocket status push | Periodic status report |
| `sourcesOnUniverse(universe, windowMs)` | `hasConflict()`, `isMerging()` | During source-status computation |
| `hasConflict()` | WebSocket status push | Periodic status report |
| `isMerging()` | WebSocket status push | Periodic status report |
| `sourceStatus()` | Frame Router (per-frame) | After each routed frame |

### Input Data (read-only)

- **Network frame** (from Frame Router): source IP, protocol byte (Art-Net or sACN), 15-bit universe, E1.31 priority (0–255), decoded DMX slot data (up to 512 bytes), slot count.
- **Config outputs** (`cfg.outputs`): `enabled` flag and 15-bit port address / universe mapping — read for slot eviction and universe mapping decisions.

### Output Data (produced)

| Target | What | Reader |
|---|---|---|
| Per-sender cache entry (ip, proto, universe, priority, data[512], dataLen, fps, lastMs) | Cached frame metadata and slot data | Merge Engine (reads `senders[i].data`, `dataLen`, `priority`, `universe`, `lastMs`) |
| Source-status value (0/1/2) | Normal / conflict / merging code | Frame Router (sets `stats().srcStatus`) |
| Active sender count (integer) | Number of sources active within 5000 ms window | WebSocket status push |

## 3. State Machine

Each of the `MAX_SENDERS` slots is in one of three implicit states:

| State | Condition | Behavior |
|---|---|---|
| **Empty** | Slot is unused (ip == 0) | Available for immediate allocation to any new sender |
| **Active** | ip != 0, `lastMs` within `SOURCE_TIMEOUT_MS` (2500 ms) of current time | Sender is contributing live data; selected by merge engine if its universe matches an enabled output |
| **Stale** | ip != 0, `lastMs` older than `SOURCE_TIMEOUT_MS` | Slot remains occupied; evicted by slot-reuse policy when a new sender needs the slot |

A slot transitions Empty ? Active when `updateSender()` allocates it (either a fresh empty slot or an evicted slot). A slot transitions Active ? Stale when no frames arrive within 2500 ms. A slot transitions Stale ? Empty (or Active ? Active if reallocated) via the slot-allocation policy in `updateSender()`.

No explicit shutdown state exists — the table persists for the device lifetime and stale entries are evicted lazily.

## 4. Data Flow

1. **Frame decoded** (core 0): Art-Net or sACN layer extracts source IP, protocol, universe, priority, and decoded DMX slot data.
2. **Frame routed** (core 0): Frame Router receives the decoded frame and determines which outputs' universes match.
3. **Sender updated** (core 0): Frame Router calls `updateSender(ip, proto, universe, priority, data, length)`.
4. **Slot allocation**: `updateSender()` applies the four-tier slot policy: (1) match existing (same ip + proto), (2) empty slot, (3) slot whose universe is no longer mapped to any enabled output, (4) oldest sender by `lastMs`.
5. **Fresh sender reset**: If the slot was not a match (newly allocated or evicted), `data` is zeroed and the FPS window counters (`winMs`, `winCnt`, `fps`) are reset.
6. **Frame cached**: `lastMs` (timestamp), `universe`, `priority`, and clamped `dataLen` are written, then slot data is `memcpy`'d into the 512-byte `data` buffer.
7. **Input frame counter**: Per-output `inFrameCnt` is incremented for each enabled output whose universe matches the sender's universe.
8. **FPS window roll**: Every 1000 ms of activity, `fps` is recomputed from the frame count within the window and the window resets.
9. **Source status computed**: Frame Router calls `sourceStatus()` ? `hasConflict()` / `isMerging()` ? `sourcesOnUniverse()` to produce a 0/1/2 status code, which is written to `stats().srcStatus`.
10. **Merge** (core 0 or core 1): Frame Router calls `mergeOutput(i)` for each matching output; the Merge Engine reads sender entries from the table.

## 5. Configuration Integration

| Field | CFG flag | Default | Read in |
|---|---|---|---|
| `outputs[i].enabled` | CFG_LIVE | board template | `universeMapped()` — for slot eviction |
| `outputs[i].universe` (via port address) | CFG_LIVE | board template | `universeMapped()` — 15-bit address comparison |
| `outputs[i].net` / `subnet` | CFG_LIVE | board template | `universeMapped()` — 15-bit address comparison |

All fields are `CFG_LIVE` — the tracker picks up changes on the next `updateSender()` or `sourcesOnUniverse()` call without a reboot. The tracker performs **no config writes**.

## 6. Lifecycle

- **Init**: The sender table is zero-initialized at system startup. All slot `ip` fields are `0`, meaning all slots are empty. No explicit init function.
- **Per-packet**: `updateSender()` is called on every routed network frame (core 0).
- **Per-tick**: `activeSenderCount()` and merge-time sender reads occur periodically via the 1 ms task tick (core 1).
- **Shutdown**: None. The table persists for the device lifetime; stale entries are evicted lazily by the slot-reuse policy.

## 7. Error Handling

- `updateSender()` returns `void` — no error return. Slot allocation always succeeds: if all slots are occupied and none are evictable, the oldest slot (by `lastMs`) is reused.
- `sourcesOnUniverse()` returns an integer count — 0 if no active sources match, positive otherwise.
- `hasConflict()` and `isMerging()` return `bool` — `false` if no enabled outputs exist or all outputs are in `MERGE_OFF` mode.
- `sourceStatus()` returns a 3-state code: 0 = normal, 1 = conflict, 2 = merging.
- `activeSenderCount()` returns an integer — the count of senders active within the 5000 ms display window.
- No logging is performed by this module — all error or edge conditions are silent and handled by the caller.

## 8. Timing Constraints

| Constraint | Value |
|---|---|
| `MAX_SENDERS` | 16 (configurable via compile-time switch) |
| `SOURCE_TIMEOUT_MS` | 2500 ms (2.5 s) — stale-sender threshold |
| Active-sender display window | 5000 ms (5 s) — used by `activeSenderCount()` for WebSocket reporting |
| FPS computation window | 1000 ms (1 s) — `fps` recomputed and window reset every second |
| `updateSender()` slot scan | O(16) — linear scan for matching (ip, proto) |
| Worst-case `updateSender()` | 3 x O(16) = 48 comparisons — when no match, no empty slot, no unmapped-universe slot, falls to oldest eviction |
| `sourcesOnUniverse()` scan | O(16) per call |
| `hasConflict()` / `isMerging()` | O(4 outputs x 16 senders) = 64 comparisons per call (each output queries `sourcesOnUniverse()`) |
| `sourceStatus()` worst case | 128 comparisons (chained `hasConflict()` + `isMerging()`) |

All operations complete well within the 1 ms DMX task tick budget and the 2 ms network receive cycle.

## 9. Memory & Allocation Model

- The sender table is a **static DRAM** allocation — a fixed array of `MAX_SENDERS` (16) `Sender` entries. Total size: 16 x sizeof(Sender) ˜ 9.4 KB.
- Each `Sender` entry contains a 512-byte `data` buffer — the dominant per-entry cost — plus metadata fields (ip, proto, timestamps, fps, universe, priority, dataLen, window counters).
- **No heap allocation** anywhere in the module. All sender data is statically allocated for the device lifetime.

## 10. Safety Considerations

- **Cross-core without a mutex**: `updateSender()` writes on core 0 (from the network receive path) and the Merge Engine reads the sender table on core 1 (from the 1 ms DMX task). There is no explicit synchronization (no mutex, no seqlock on the sender table).
- **Atomicity guarantee**: 32-bit aligned scalar writes are atomic on the ESP32. The only multi-byte write within a `Sender` entry is the 512-byte `data` buffer `memcpy`.
- **Torn-read prevention**: `dataLen` is updated **after** the `data` `memcpy` completes. This ordering invariant guarantees that a core-1 reader never observes a `dataLen` value larger than the data currently stored in the buffer — it always sees a consistent length paired with the matching data.
- **Stale-entry eviction**: The four-tier slot policy ensures that stale or unmapped senders are reclaimed without blocking, preventing table saturation under sender churn.
- **No lock overhead**: The lock-free design avoids priority inversion and deadlock risks on the time-critical core 1 path. The output DMX buffer (protected by a seqlock) is the synchronization boundary for transmitted data, not the sender table itself.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| core.frame-router | calls `updateSender()`, `sourceStatus()` | Primary producer — invokes `updateSender()` on every routed frame and `sourceStatus()` to set `stats().srcStatus` |
| core.merge-engine | reads `senders[i]` | Consumes sender data (data, dataLen, priority, universe, lastMs) for contributor selection and frame copying |
| net.artnet | calls `updateSender()` | Direct calls for staged ArtSync commit path |
| net.sacn | calls `updateSender()` | Direct calls for staged frame and sync-loss commit paths |
| sys.tasks | reads `lastMs` | Task tick reads sender activity timestamps for timeout/loss detection |
| net.websocket | calls `activeSenderCount()`, `hasConflict()`, `isMerging()` | Periodic status reporting to WebSocket clients |
| cfg.config-schema | read | `outputs[].enabled` and universe/port-address mapping for slot eviction |
| core.dmx-buffer | (indirect) | Sender `data` is the source for seqlock-protected output buffer writes in the merge engine |

## 12. Testing Verification

| Test Case | File:Line | Validates |
|---|---|---|
| HTP merge setup | merge_test.cpp | `updateSender()` caches frames and priority for multi-source HTP merge |
| OFF / last-frame-wins | merge_test.cpp | `updateSender()` with same-universe sender updates in place (slot match path) |
| Cross-universe isolation | merge_test.cpp | Only senders whose `universe` matches the output port address are selected (indirectly via `updateSender()` universe field) |
| LTP-Takeover priority | merge_test.cpp | Sender priority and `lastMs`-based selection after `updateSender()` |
| Priority merge | merge_test.cpp | Top-priority sender selection across multiple cached senders |
| Failsafe timeout | merge_test.cpp | `lastMs`-based timeout detection and stale sender handling |
| LastMs manipulation | merge_test.cpp | Test directly sets `senders[0].lastMs` to simulate timing — bypasses `updateSender()` for timing control |

**Untested directly**: `universeMapped()`, `activeSenderCount()`, `sourcesOnUniverse()`, `hasConflict()`, `isMerging()`, and `sourceStatus()` have no dedicated unit tests. They are covered only indirectly through the merge engine's use of `senders[i].lastMs` and the `sourceStatus()` value set by the Frame Router. No dedicated `sender_tracker_test.cpp` exists.

## 13. Open Questions

1. **WebSocket caller identity**: The exact WebSocket status-reporting functions that call `activeSenderCount()`, `hasConflict()`, `isMerging()`, and `sourceStatus()` are not documented in the inspected code; their integration points are declared but not traced.
2. **Table reset policy**: Whether the sender table is ever explicitly cleared/reset on a full network disconnect or configuration change (e.g., output universe remapping), or whether slot reclamation relies solely on the timeout window and lazy eviction.
3. **dataLen race invariant**: While the write ordering (data memcpy first, then dataLen) is clearly intentional, there is no documented comment asserting this invariant. A future refactor that reorders these writes could silently introduce torn-read exposure on core 1.

## 14. History

- **Initial design**: Fixed 16-slot sender table keyed by (source IP, protocol) to track active network DMX sources.
- **Protocol byte added**: An 8-bit protocol discriminator (0 = Art-Net, 1 = sACN) was introduced to allow coexisting Art-Net and sACN sources on the same universe without key collision.
- **Slot eviction policy**: The four-tier allocation order (match ? empty ? unmapped universe ? oldest) was added to handle sender churn under the 16-slot limit without requiring a timeout-based hard clear.
- **FPS window calculation**: A 1-second rolling frame-rate computation was added for WebSocket status reporting.
- **Source-status API**: The `sourceStatus()` function (returning 0 = normal, 1 = conflict, 2 = merging) was introduced to expose sender conflict and merging state as a compact code for the web UI.
- **Display-window decoupling**: The 5000 ms window in `activeSenderCount()` was deliberately decoupled from `SOURCE_TIMEOUT_MS` (2500 ms) — the former is for WebSocket "active sender" display, the latter is for merge-loss detection.

## Known Limitations

- **Cross-core without a lock**: There is no explicit synchronization between the core 0 writer (`updateSender`) and core 1 reader (merge engine). Torn reads of the 512-byte `data` buffer are theoretically possible if the core 1 reader reads mid-memcpy, though the `dataLen`-after-`data` write ordering mitigates this for length-paired reads.
- **Fixed 512-byte data buffer**: Each `Sender.data` is fixed at 512 bytes — supports full DMX512 (513 slots including start code, which is stripped before caching) but no larger payloads.
- **Universe-remap eviction**: A still-active sender can be evicted if its universe is no longer mapped to any enabled output, even if it is within the activity window. This is by design for universe remapping, but means rapid sender turnover on a shared universe can churn the table.
- **Decoupled timeouts**: `activeSenderCount()` uses a 5000 ms display window while `SOURCE_TIMEOUT_MS` is 2500 ms for merge-loss logic. The two timeouts serve different purposes but are not documented as intentionally distinct outside this spec.
