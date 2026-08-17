# Merge Engine — Technical Reference

Domain: core.merge-engine

## 1. Domain Scope

Owns the per-output DMX frame merging logic that runs after `updateSender()` caches a new inbound frame and before the DMX transmit task clocks the buffer out. The module selects which sender(s) contribute to an output based on the configured merge mode and E1.31 priority, then writes the merged result into the seqlock-protected output buffer.

Merge modes handled (declared in `include/config_enums.h:5`):
- `MERGE_OFF` (0) — last-seen live source wins the whole frame.
- `MERGE_HTP` (1) — per-channel highest-takes-priorities across top-priority sources.
- `MERGE_LTP` (2) — most recently seen top-priority source wins.
- `MERGE_LTP_TAKEOVER` (3) — highest-priority source wins; ties broken by newest (`lastMs`).
- `MERGE_PRIORITY` (4) — only top-priority sources contribute, per-channel max.

On source loss, the per-output `lossMode` (`include/config_enums.h:6`) determines the fallback:
- `LOSS_HOLD` (0) — buffer retains last frame (do nothing).
- `LOSS_ZERO` (1) — zero all 512 slots.
- `LOSS_STOP` (2) — no buffer write; transmit task halts via `outSrcLost[]` + `txStyle` gating.
- `LOSS_PRESET` (3) — recall a scene preset.
- `LOSS_HOME` (4) — recall the home scene.

Delegates scene recall to `[core-scene-engine](./core-scene-engine.md)` and alerting to `sys-alert`. Does not touch hardware.

Consumers:
- `[core-frame-router](./core-frame-router.md):23,34` — calls `mergeOutput(i)` after routing a live frame.
- `sys-tasks` — calls `mergeOutputTimed()` every tick when no output is in delta mode (`src/sys/tasks.cpp:135`).
- `[net-artnet-protocol](./net-artnet-protocol.md):247` — calls `mergeOutput(i)` inside `commitArtSyncStaged()`.

## 2. Layer Mapping

```
drv  →  cfg  →  core  →  net  →  app/sys
         ↑      ↑       ↑        ↑
         |      |       |        |
         |      reads   |        |
         |      cfg     |        |
         |      (merge   |        |
         |      lossMode)|        |
         |               |        |
         |             writes ———|—— reads (outSrcLost[],
         |               buffer |   outLastDmxMs[])
```

The merge engine is a **core** layer consumer of `cfg` (`DmxOutput` fields) and `core` layer producer for the seqlock DMX buffer. It is called from both **net** layer (ArtSync commit) and **sys** layer (task tick).

## 3. Source Files

| File | Role |
|---|---|
| `src/core/merge_engine.cpp` | `mergeOutput()` (line 16), `mergeOutputTimed()` (line 122), `portFailsafeMs()` helper (line 12) |
| `src/core/merge_engine.h` | Declarations for `mergeOutput()` (line 13) and `mergeOutputTimed()` (line 15) |
| `include/config_enums.h:5-6` | `MERGE_*` and `LOSS_*` enum constants |
| `include/config_schema.h:22-34` | `DmxOutput` struct definition with merge/loss fields |

## 4. Data Structures

### Inputs (read-only)

| Struct/Field | Location | Used For |
|---|---|---|
| `cfg.outputs[outIdx].enabled` | `include/config_schema.h:47` | Skip disabled outputs (`src/core/merge_engine.cpp:125`) |
| `cfg.outputs[outIdx].mergeMode` | `include/config_schema.h:22` | Select merge algorithm (`src/core/merge_engine.cpp:62,77,93,108`) |
| `cfg.outputs[outIdx].lossMode` | `include/config_schema.h:23` | Select fail-safe on source loss (`src/core/merge_engine.cpp:36`) |
| `cfg.outputs[outIdx].lossPreset` | `include/config_schema.h:24` | Index into scene table for `LOSS_PRESET` (`src/core/merge_engine.cpp:46`) |
| `cfg.outputs[outIdx].failsafeTimeout` | `include/config_schema.h:25` | Per-output source-loss timeout override (`src/core/merge_engine.cpp:13`) |
| `Sender.ip`, `.universe`, `.priority`, `.data`, `.dataLen`, `.lastMs` | `src/core/sender_tracker.h:13-24` | Source data to merge |
| `portAddress(const DmxOutput&)` | `src/core/frame_router.h:8` | Compares output's 15-bit address to sender's universe (`src/core/merge_engine.cpp:25`) |

### Outputs (written)

| Target | Location | What |
|---|---|---|
| `dmxBufferState().buffers[outIdx].data[1..512]` | `src/core/dmx_buffer.h:12` | Merged frame bytes |
| `stats().outSrcLost[outIdx]` | `src/core/stats.h:33` | Source-loss flag (true when no contributors) |
| `stats().rxLossCount[outIdx]` | `src/core/stats.h:35` | Increments on each tick with loss (`src/core/merge_engine.cpp:31`) |
| `stats().srcStatus` | `src/core/stats.h:42` | Updated via `sourceStatus()` in frame_router (`src/core/frame_router.cpp:39`) |

## 5. Concurrency

**Single-threaded (core 1, `dmxTxTask` + `mergeOutputTimed`; core 0 via `routeFrame` and ArtSync commit).**

- `mergeOutput()` is called from `routeFrameImpl()` on core 0 (`src/core/frame_router.cpp:23`) and from `snapshotAndTransmit` path indirectly via `dmxFrameTick()` on core 1 (`src/sys/tasks.cpp:131`). Both cores can call it, but the seqlock-protected write (`dmxBufWriteBegin/WriteEnd`) makes the buffer mutation safe — the reader (`dmxBufSnapshot` at `src/sys/tasks.cpp:100`) will retry if it observes an odd seqlock ticket.
- `mergeOutputTimed()` is called from `dmxFrameTick()` on core 1 only (`src/sys/tasks.cpp:122-135`). When no output uses delta TX style, it scans all enabled outputs for timed-out sources and calls `mergeOutput(i)` (`src/core/merge_engine.cpp:135` via `src/sys/tasks.cpp:135`).
- `millis()` is the time source (`src/core/merge_engine.cpp:18,126`); it is FreeRTOS-safe but not atomic across a read-modify of the seqlock — the seqlock handles that.
- `alertSourceLost`/`alertSourceRestored` may trigger a blocking HTTP POST (webhook). These are called on core 0 (via `routeFrame` path) and core 1 (via `mergeOutputTimed` path). The webhook POST is performed by `alert.cpp` — whether it is non-blocking is not determinable from the inspected merge source (see [Open Questions](#18-open-questions)).

## 6. State Machine

`mergeOutput()` itself is stateless — it computes the merged frame for one output from the current sender table and writes it. The implicit state machine lives in the caller:

- **Source active** (≥1 contributor within `failsafeTimeout`): merge and write buffer; set `outSrcLost[i] = false`; call `alertSourceRestored`.
- **Source lost** (0 contributors): set `outSrcLost[i] = true`; increment `rxLossCount[i]`; call `alertSourceLost`; apply `lossMode` branch (zero/stop/preset/home/hold); return without writing a merged frame (except `LOSS_ZERO` which writes zeros).

## 7. Entry Points

| Entry point | Address | Caller |
|---|---|---|
| `mergeOutput(int outIdx)` | `src/core/merge_engine.cpp:16` | `routeFrameImpl()` (`src/core/frame_router.cpp:23,34`); `commitArtSyncStaged()` (`src/net/artnet.cpp:247`); indirectly `snapshotAndTransmit` via `dmxFrameTick` (`src/sys/tasks.cpp:131`) |
| `mergeOutputTimed()` | `src/core/merge_engine.cpp:122` | `dmxFrameTick()` (`src/sys/tasks.cpp:135`) when no output is in delta TX mode |
| `portFailsafeMs(const DmxOutput&)` | `src/core/merge_engine.cpp:12` (inline) | `mergeOutput()` line 19; `mergeOutputTimed()` line 126 |

## 8. Data Flow

1. **Sender updates** (core 0, on packet arrival): `updateSender()` populates `SenderTracker::senders[i]` with new data, priority, `lastMs`, `universe` (`src/core/sender_tracker.cpp:16-56`). Called from `routeFrameImpl()` at `src/core/frame_router.cpp:14`.
2. **Route frame** (core 0): `routeFrameImpl()` calls `mergeOutput(i)` for each matching output (`src/core/frame_router.cpp:23`), OR
3. **Task-driven timed merge** (core 1): `dmxFrameTick()` calls `mergeOutputTimed()` which scans all senders for each enabled output; if a sender has gone silent (exceeded `failsafeTimeout`), it calls `mergeOutput(i)` (`src/core/merge_engine.cpp:122-134`, `src/sys/tasks.cpp:135`).
4. **Contributor selection**: `mergeOutput()` iterates `MAX_SENDERS` (`src/core/sender_tracker.h:9`), filters by `ip != 0` and `universe == portAddress(out)` (`src/core/merge_engine.cpp:23-29`), and filters by `now - lastMs < failsafeTimeout` (`src/core/merge_engine.cpp:26`). If zero contributors: branch to loss handling.
5. **Priority filtering**: `topPrio` is computed from contributing senders (`src/core/merge_engine.cpp:28`). Only senders with `priority >= topPrio` participate in the merge (`src/core/merge_engine.cpp:98`).
6. **Merge algorithm dispatch**:
   - `MERGE_LTP_TAKEOVER` (line 62): picks highest-priority sender; if tie, newest `lastMs` (`src/core/merge_engine.cpp:62-74`).
   - `MERGE_PRIORITY` (line 77): per-channel max across all top-priority senders (`src/core/merge_engine.cpp:77-91`).
   - `MERGE_HTP` with ≥2 sources (line 93): per-channel max across top-priority senders, 512 slots (`src/core/merge_engine.cpp:93-106`).
   - Default (last branch, line 108): newest top-priority sender wins whole frame (`src/core/merge_engine.cpp:108-119`).
7. **Buffer write**: winner frame is `memcpy`'d into `buffers[outIdx].data[1..512]` wrapped in `dmxBufWriteBegin/WriteEnd` seqlock (`src/core/merge_engine.cpp:38-40, 70-72, 87-89, 102-104, 116-118`).
8. **Loss handling** (`src/core/merge_engine.cpp:30-57`): if no contributors, applies `lossMode` → `LOSS_ZERO` (zero 512 slots), `LOSS_PRESET` (sceneRecall), `LOSS_HOME` (sceneRecallHome), `LOSS_STOP`/`LOSS_HOLD` (do nothing, buffer retains).
9. **Alert**: `alertSourceLost`/`alertSourceRestored` called on transition (`src/core/merge_engine.cpp:35,59`).

## 9. Protocol Layout

N/A (no wire protocol). The merge engine consumes decoded DMX slot data (513 bytes: start code + 512 slots) from the sender tracker and produces merged DMX slot data into the buffer. The wire protocol for receiving these frames is documented in `[net-artnet-protocol](./net-artnet-protocol.md)` and `[net-sacn-protocol](./net-sacn-protocol.md)`.

## 10. Config Integration

All reads from the `Config` struct (`include/config_schema.h:38-48`), no writes.

| Field | CFG_* flag | Default source | Read in |
|---|---|---|---|
| `outputs[i].enabled` | `CFG_LIVE` | board template (`include/config_types.h:37`) | `mergeOutputTimed` (`src/core/merge_engine.cpp:125`) |
| `outputs[i].mergeMode` | `CFG_LIVE` | board template (`include/config_types.h:22`) | `mergeOutput()` lines 62, 77, 93, 108 |
| `outputs[i].lossMode` | `CFG_LIVE` | board template (`include/config_types.h:23`) | `mergeOutput()` line 36 |
| `outputs[i].lossPreset` | `CFG_LIVE` | board template (`include/config_types.h:24`) | `mergeOutput()` line 46 |
| `outputs[i].failsafeTimeout` | `CFG_LIVE` | board template (`include/config_types.h:25`) | `portFailsafeMs()` line 13 |

Merge mode and loss mode apply live — the next call to `mergeOutput()` or `mergeOutputTimed()` picks up the new value without a reboot (`include/config_types.h:22-25` flags: `CFG_LIVE` per the schema table). Pin/GPIO settings (txPin, rxPin, rtsPin) are `CFG_REBOOT` and are not read by this module.

## 11. Lifecycle

- **Init**: No explicit init function. The module reads `cfg` and `senderTracker()` state at call time.
- **Per-frame hook**: `mergeOutput(int outIdx)` is called immediately after `updateSender()` caches a new frame (`src/core/frame_router.cpp:14-23`).
- **Per-tick hook**: `mergeOutputTimed()` is called from `dmxFrameTick()` (`src/sys/tasks.cpp:122`) — but only when no output is in `TXSTYLE_DELTA` mode (`src/sys/tasks.cpp:135`), because delta outputs are merged on-demand per-packet.
- **Shutdown**: None. The module holds no resources.

## 12. Error Handling

- `mergeOutput()` returns `void` — no error return. Failures are silent (e.g., if `outIdx` is out of bounds, the caller is responsible; `senderTracker().senders[contrib[best]]` is safe because `contrib[k]` is always `< MAX_SENDERS`).
- `portFailsafeMs()` returns `SOURCE_TIMEOUT_MS` (2500) when `failsafeTimeout` is 0 (line 13-14) — a neutral-value fallback, not an error.
- `LOSS_PRESET` calls `sceneRecall()` which is a no-op if the scene table is not loaded (`include/config_enums.h:46-48` — scene engine returns `void`); the buffer simply retains its last value.
- The `alertSourceLost`/`alertSourceRestored` calls (`src/core/merge_engine.cpp:35,59`) do not check return values — webhook failure is non-fatal.
- `ip` variable at `src/core/merge_engine.cpp:34` is a dead pointer: `(nc > 0 && contrib[0] < MAX_SENDERS) ? nullptr : nullptr` — always `nullptr`. This is a pre-existing code defect, not an error path.

## 13. Memory Allocation

- `merged[256]` (`src/core/merge_engine.cpp:79`) — stack-allocated temporary for `MERGE_PRIORITY` (256 bytes, because sACN/ArtNet priority is 256-slot max for legacy ArtDMX 8-bit).
- `merged[512]` (`src/core/merge_engine.cpp:94`) — stack-allocated temporary for `MERGE_HTP` (512 bytes).
- `contrib[MAX_SENDERS]` (`src/core/merge_engine.cpp:21`) — stack-allocated index array (64 bytes for `MAX_SENDERS=16`).
- `g_senderTracker` is a static global at `src/core/sender_tracker.cpp:7` — its `senders[MAX_SENDERS]` array (16 × 585 bytes ≈ 9.4 KB) lives in DRAM.
- No heap allocation anywhere in this module.

## 14. Timing

- `mergeOutput()` scans up to `MAX_SENDERS` (16) senders per call (`src/core/sender_tracker.h:9`, `src/core/merge_engine.cpp:23`). For each enabled output, this is O(16) comparisons + O(1) memcpy of 512 or 256 bytes.
- `mergeOutputTimed()` iterates all `MAX_OUTPUTS` (4) × `MAX_SENDERS` (16) = 64 comparisons per tick (`src/core/merge_engine.cpp:122-134`). Called at 1 ms tick on core 1.
- `portFailsafeMs()` converts `failsafeTimeout` seconds → ms (`src/core/merge_engine.cpp:13`); `SOURCE_TIMEOUT_MS` is 2500 ms (`src/core/sender_tracker.h:10`).
- The merge happens on core 0 (via `routeFrame`) or core 1 (via `dmxFrameTick`); either path writes via seqlock so the core-1 reader is never torn.
- Best-effort, no hard deadline — the merge completes well within the 1 ms DMX tick period.

## 15. Traceability

| Claim | Evidence |
|---|---|
| Merge modes defined as enum constants | `include/config_enums.h:5` |
| Loss modes defined as enum constants | `include/config_enums.h:6` |
| `SOURCE_TIMEOUT_MS` is 2500 | `src/core/sender_tracker.h:10` |
| `MAX_SENDERS` defaults to 16 | `src/core/sender_tracker.h:9` |
| `portFailsafeMs` falls back to `SOURCE_TIMEOUT_MS` when timeout is 0 | `src/core/merge_engine.cpp:12-14` |
| `mergeOutput` iterates senders and filters by universe + timeout | `src/core/merge_engine.cpp:21-29` |
| `topPrio` extracted from contributing senders | `src/core/merge_engine.cpp:28-29` |
| `LOSS_ZERO` zeros 512 bytes via seqlock | `src/core/merge_engine.cpp:38-41` |
| `LOSS_PRESET` calls `sceneRecall` | `src/core/merge_engine.cpp:46` |
| `LOSS_HOME` calls `sceneRecallHome` | `src/core/merge_engine.cpp:49` |
| `LOSS_STOP` and `LOSS_HOLD` are no-ops (handled by tx task) | `src/core/merge_engine.cpp:42-54` |
| `MERGE_LTP_TAKEOVER` picks highest priority, tie-break on `lastMs` | `src/core/merge_engine.cpp:62-74` |
| `MERGE_PRIORITY` does per-channel max on 256 bytes | `src/core/merge_engine.cpp:77-91` |
| `MERGE_HTP` does per-channel max on 512 bytes (only when ≥2 sources) | `src/core/merge_engine.cpp:93-106` |
| Default branch: newest top-priority sender wins | `src/core/merge_engine.cpp:108-119` |
| All buffer writes bracketed by `dmxBufWriteBegin/WriteEnd` | `src/core/merge_engine.cpp:38,40,70,72,87,89,102,104,116,118` |
| `outSrcLost` cleared on active source | `src/core/merge_engine.cpp:58` |
| `alertSourceLost` called on source loss | `src/core/merge_engine.cpp:35` |
| `alertSourceRestored` called on source return | `src/core/merge_engine.cpp:59` |
| `rxLossCount` incremented on every loss tick | `src/core/merge_engine.cpp:31` |
| `mergeOutputTimed` called only when no delta output active | `src/sys/tasks.cpp:135` |
| Dead `ip` pointer at line 34 — always `nullptr` | `src/core/merge_engine.cpp:34` |

## 16. Cross-References

- `[core-frame-router](./core-frame-router.md)` — calls `mergeOutput()` after routing each frame (`src/core/frame_router.cpp:23,34`); also calls `routeFrame` which calls `updateSender`.
- `[core-sender-tracker](./core-sender-tracker.md)` — provides the `SenderTracker` and `Sender` structs that `mergeOutput()` reads (`src/core/sender_tracker.cpp:7-97`).
- `[core-dmx-buffer](./core-dmx-buffer.md)` — the seqlock-protected `DmxBufferState` that receives merged data via `dmxBufWriteBegin/WriteEnd` (`src/core/merge_engine.cpp:38-40`).
- `[core-scene-engine](./core-scene-engine.md)` — `sceneRecall()` and `sceneRecallHome()` are called on `LOSS_PRESET` and `LOSS_HOME` (`src/core/merge_engine.cpp:46-49`).
- `core-stats` — `stats()` accessor for `outSrcLost`, `rxLossCount` (`src/core/merge_engine.cpp:31,58`); module doc not yet written (see [Open Questions](#18-open-questions)).
- `sys-alert` — `alertSourceLost`/`alertSourceRestored` for webhook alerts (`src/core/merge_engine.cpp:35,59`).
- `sys-tasks` — `dmxFrameTick()` calls `mergeOutputTimed()` and `sceneFadeStep()` (`src/sys/tasks.cpp:122-136`).
- `[net-artnet-protocol](./net-artnet-protocol.md)` — `commitArtSyncStaged()` calls `mergeOutput(i)` (`src/net/artnet.cpp:247`).

## 17. Limitations

- `mergeOutput()` has a dead `ip` pointer at `src/core/merge_engine.cpp:34` that is always `nullptr` — the source IP string is never passed to `alertSourceLost`, so webhook alerts cannot report the lost source's IP.
- The `merged[256]` array in the `MERGE_PRIORITY` path (`src/core/merge_engine.cpp:79`) limits priority merge to 256 slots, while `MERGE_HTP` uses the full 512 (`src/core/merge_engine.cpp:94`). This asymmetry is intentional for E1.31 priority semantics but is a subtle constraint.
- `LOSS_STOP` is handled only by the transmit task via `outSrcLost[]` + `txStyle` gating (`src/sys/tasks.cpp:98`), not by writing anything here (`src/core/merge_engine.cpp:42-44`).
- The merge engine reads `cfg` without a lock — `cfg` is mutated only during config load or web-apply and is not expected to change mid-merge, but this is not explicitly synchronized.

## 18. Open Questions

1. Not determinable from the inspected source code — whether `alertSourceLost`'s webhook POST is non-blocking (does it spawn a task or POST synchronously on the calling core?). The merge engine calls it without error checking at `src/core/merge_engine.cpp:35`.
2. Not determinable from the inspected source code — the exact behavior of `sceneRecall()` when `lossPreset` is out of bounds; the merge engine calls it at `src/core/merge_engine.cpp:46` but the scene engine's bounds checking is in `[core-scene-engine](./core-scene-engine.md)` which was not fully inspected.
3. Not determinable from the inspected source code — whether the `merged[256]` limit in `MERGE_PRIORITY` is a deliberate spec alignment (E1.31 priority covers 256 slots) or a latent truncation bug. See `src/core/merge_engine.cpp:78-88`.

## 19. Testing

- `test/native/merge_test.cpp` — validates HTP merge (`test/native/merge_test.cpp:26-43`), OFF/last-frame-wins (`test/native/merge_test.cpp:45-61`), `LOSS_ZERO` (`test/native/merge_test.cpp:63-76`), cross-universe isolation (`test/native/merge_test.cpp:78-92`), `MERGE_LTP_TAKEOVER` (`test/native/merge_test.cpp:94-116`), `MERGE_PRIORITY` (`test/native/merge_test.cpp:118-136`), per-port failsafe timeout default (`test/native/merge_test.cpp:138-153`), per-port failsafe timeout override (`test/native/merge_test.cpp:155-169`), `LOSS_PRESET` fallback (`test/native/merge_test.cpp:171-183`), and `LOSS_HOME` fallback (`test/native/merge_test.cpp:185-196`).
- `mergeOutputTimed()` has no direct unit test — it depends on `millis()` and the sender table state, which is exercised only on hardware.
- No dedicated `merge_engine_test.cpp` exists separate from the combined `merge_test.cpp` host test.

## 20. History

- Initial merge modes (`MERGE_OFF`, `MERGE_HTP`, `MERGE_LTP`) defined in `include/config_enums.h:5` to match legacy LuxDMX behavior.
- `MERGE_LTP_TAKEOVER` and `MERGE_PRIORITY` added to support E1.31 priority semantics (`src/core/merge_engine.cpp:62,77`) — these allow a high-priority controller to preempt a lower-priority live source.
- `LOSS_PRESET` and `LOSS_HOME` added to invoke the scene engine on signal loss (`src/core/merge_engine.cpp:46-49`).
- `LOSS_STOP` path deferred to the transmit task's `outSrcLost` check (`src/core/merge_engine.cpp:42-44`, `src/sys/tasks.cpp:98`).
- `mergeOutputTimed()` added to handle timed source-loss detection for non-delta outputs (`src/core/merge_engine.cpp:122-134`).
