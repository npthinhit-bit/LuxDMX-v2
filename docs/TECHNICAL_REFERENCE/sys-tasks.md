# Tasks — Technical Reference

Domain: sys.tasks

## 1. Domain Scope

Owns FreeRTOS task lifecycle for the entire firmware: creation (`createTasks`), the two core-pinned data-path tasks (`dmxTxTask`, `netRxTask`), the low-priority housekeeping tasks (`ledTask`, `displayTask`, `versionCheckTask`), and the per-tick DMX frame engine (`dmxFrameTick`, `snapshotAndTransmit`). Also owns the NVS-backed crash-guard counter (`dmxInitGuardBegin`/`dmxInitGuardEnd`) and the output-rate lookup tables (`DMX_RATE_MS`, `dmxPeriodMs`, `dmxRateHz`).

Delegates:
- LED rendering to `[sys-led-status](./sys-led-status.md)` (`initLed`, `setLedColor`).
- Display rendering to `[sys-display]` (`renderDisplay` is an empty stub here; real impl in `src/sys/display.cpp`).
- RDM version check to `[sys-firmware-version](./sys-firmware-version.md)` (`versionCheck`).
- DMX transmission to `[drv-dmx-rmt-tx](./drv-dmx-rmt-tx.md)` (`rmtDmxIdle`, `rmtDmxKick`).
- Buffer snapshot to `[core-dmx-buffer](./core-dmx-buffer.md)` (`dmxBufSnapshot`).
- ArtNet drain/dispatch to `[net-artnet-protocol](./net-artnet-protocol.md)` (`artRdmPollRx`, `artPktDispatchAll`, `artRdmDrainResponses`).
- sACN drain to `[net-sacn-protocol](./net-sacn-protocol.md)` (`readSacn`).
- Merge to `[core-merge-engine](./core-merge-engine.md)` (`mergeOutputTimed`).
- Scenes to `[core-scene-engine](./core-scene-engine.md)` (`sceneFadeStep`).
- Stats to `[core-stats](./core-stats.md)` (`stats()`).
- ArtSync staging to `[net-artnet-protocol](./net-artnet-protocol.md)` (`commitArtSyncStaged`, `artNet().syncMode`/`syncLastMs`).

Consumers:
- `src/main.cpp:106-108` — calls `dmxInitGuardBegin()` → `outputInitAll()` → `dmxInitGuardEnd()`.
- `src/main.cpp:130` — calls `createTasks()`.
- `src/main.cpp:110,112` — calls `syslogInit()` and `soakInit()` (housekeeping init adjacent to task spawn).
- `src/sys/tasks.cpp:114` — `flushArtSyncStaged` reads `artNet().syncMode` and calls `commitArtSyncStaged`.
- `src/net/web_server.cpp:48` — `soakStatsJson()` is consumed by the `/diag/soak-stats` route (see [sys-soak-monitor](./sys-soak-monitor.md)).

## 2. Layer Mapping

```
drv  →  cfg  →  core  →  net  →  app/sys
         ↑        ↑        ↑         ↑
       cfg     g_      artNet()   createTasks()
     (per-   outputs[]   syncMode   (setup:130)
     output  dmxBufSnapshot mergeOutputTimed
     fields) rmtDmxIdle  readSacn   dmxFrameTick
             rmtDmxKick  sceneFadeStep
```

All task code lives in the **sys** layer (`src/sys/tasks.cpp`). It is the sole consumer of the **core** DMX buffer/sequence/ merge APIs from the core-1 side and the **net** drain/dispatch APIs from the core-0 side. The rate table (`DMX_RATE_MS`) is indexed by the `cfg.outputs[i].txRate` field from the **cfg** layer (`include/config_schema.h:48`).

## 3. Source Files

| File | Role |
|---|---|
| `src/sys/tasks.cpp` | `DMX_RATE_MS[]` (line 25), `DMX_RATE_COUNT` (line 26), `dmxPeriodMs` (line 28), `dmxRateHz` (line 34), `dmxInitGuardBegin` (line 43), `dmxInitGuardEnd` (line 57), `createTasks` (line 78), `updateLedFromNet` (line 87), `renderDisplay` (line 93), `snapshotAndTransmit` (line 96), `flushArtSyncStaged` (line 110), `dmxFrameTick` (line 120), `dmxTxTask` (line 138), `netRxTask` (line 146), `ledTask` (line 157), `displayTask` (line 165), `versionCheckTask` (line 172), `s_crashCount` (line 39), `PREF_NS_TASKS` (line 40), `DMX_GUARD_TTL_MS` (line 41) |
| `src/sys/tasks.h` | `g_dmxTask` (line 5), `dmxPeriodMs`/`dmxRateHz` (lines 7-8), `createTasks`/`dmxInitGuardBegin`/`dmxInitGuardEnd`/`mergeOutputTimed`/`updateLedFromNet`/`renderDisplay` (lines 10-15), `xLastWakeTime` (line 17), `dmxTxTask`/`netRxTask`/`ledTask`/`displayTask`/`versionCheckTask` (lines 19-23) |

## 4. Data Structures

### `DMX_RATE_MS[]` (`src/sys/tasks.cpp:25`)

| Index | Value (ms) | Rate (fps) | ENUM_TXRATE label |
|---|---|---|---|
| 0 | 25 | 40 | "40 fps (25 ms)" |
| 1 | 24 | 41.7 | "41.7 fps (24 ms)" |
| 2 | 30 | 33.3 | "33.3 fps (30 ms)" |
| 3 | 40 | 25 | "25 fps (40 ms)" |
| 4 | 50 | 20 | "20 fps (50 ms)" |

Five entries; `DMX_RATE_COUNT` (`src/sys/tasks.cpp:26`) = `sizeof(DMX_RATE_MS)/sizeof(DMX_RATE_MS[0])` = 5. Indices are clamped in `dmxPeriodMs` (`src/sys/tasks.cpp:30`).

### `g_dmxTask` (`src/sys/tasks.h:5`)

`TaskHandle_t` — handle to the core-1 DMX transmit task, captured by `xTaskCreatePinnedToCore` at `src/sys/tasks.cpp:83`. Currently declared but not referenced for notification/priority manipulation in the inspected source (see [Open Questions](#18-open-questions)).

### `xLastWakeTime` (`src/sys/tasks.h:17`)

`TickType_t` — used by `vTaskDelayUntil(&xLastWakeTime, 1)` in `dmxTxTask` (`src/sys/tasks.cpp:142`) for a precise 1 ms cadence.

## 5. Concurrency

- **`dmxTxTask`** — pinned to **core 1**, priority **19** (the highest task priority in the system) (`src/sys/tasks.cpp:83`). Drives all DMX/RMT transmission and the merge engine. Period: 1 ms (1 tick) via `vTaskDelayUntil(&xLastWakeTime, 1)` at `src/sys/tasks.cpp:142`.
- **`netRxTask`** — pinned to **core 0**, priority **5** (`src/sys/tasks.cpp:84`). Drains UDP sockets (Art-Net ≤8 packets/call, sACN ≤4/socket — see `src/net/artnet.cpp:64` and `src/net/sacn.cpp`) and dispatches via the SPSC rings. Period: 2 ms via `vTaskDelay(pdMS_TO_TICKS(2))` at `src/sys/tasks.cpp:153`.
- **`ledTask`** — unpinned (runs on whichever core was free at creation), priority **1**, period 50 ms (`src/sys/tasks.cpp:79,161`). Calls into `[sys-led-status](./sys-led-status.md)`.
- **`displayTask`** — priority **1**, period 200 ms, only created when `dispReady` is true (`src/sys/tasks.cpp:80,167-168`).
- **`versionCheckTask`** — priority **1**, period 60 000 ms (`src/sys/tasks.cpp:81,175`).
- **Crash guard**: `s_crashCount` is a plain `uint8_t` (`src/sys/tasks.cpp:39`) — read/written only during `setup()` on core 0 before tasks spawn, so no synchronization is required.

Cross-core data path: `netRxTask` (core 0) writes frame data into the seqlock-staged `dmxBuffers` ([core-dmx-buffer](./core-dmx-buffer.md)); `dmxTxTask` (core 1) reads via `dmxBufSnapshot` — a single-writer/single-reader protocol with no locks ([core-dmx-buffer](./core-dmx-buffer.md)).

## 6. State Machine

No task-level state machine. `dmxFrameTick` is a level-triggered per-tick sweep: every 1 ms it iterates all outputs and transmits if the per-output period (`dmxPeriodMs`) has elapsed.

The crash guard (`dmxInitGuardBegin`/`dmxInitGuardEnd`) implements a small NVS-backed sequence:

- **Boot (counter == 0)**: `dmxInitGuardBegin` reads `s_crashCount = 0` from NVS (`src/sys/tasks.cpp:46`); no outputs disabled (`src/sys/tasks.cpp:48`).
- **Crash recovery (counter > 0)**: `dmxInitGuardBegin` disables outputs `[0 .. counter-1]` sequentially (`src/sys/tasks.cpp:50-53`).
- **Stable guard**: `dmxInitGuardEnd` writes `counter + 1` to NVS, then spins 3 000 ms (`DMX_GUARD_TTL_MS`, `src/sys/tasks.cpp:41,62-65`). Surviving 3 s resets the counter to 0 (`src/sys/tasks.cpp:69-75`); a panic during the window leaves the counter incremented so the next boot disables one more output.

## 7. Entry Points

| Entry point | Address | Caller |
|---|---|---|
| `createTasks()` | `src/sys/tasks.cpp:78` | `setup()` (`src/main.cpp:130`) |
| `dmxInitGuardBegin()` | `src/sys/tasks.cpp:43` | `setup()` before `outputInitAll` (`src/main.cpp:106`) |
| `dmxInitGuardEnd()` | `src/sys/tasks.cpp:57` | `setup()` after `outputInitAll` (`src/main.cpp:108`) |
| `dmxTxTask` | `src/sys/tasks.cpp:138` | FreeRTOS scheduler (core 1, prio 19) |
| `netRxTask` | `src/sys/tasks.cpp:146` | FreeRTOS scheduler (core 0, prio 5) |
| `ledTask` | `src/sys/tasks.cpp:157` | FreeRTOS scheduler (prio 1) |
| `displayTask` | `src/sys/tasks.cpp:165` | FreeRTOS scheduler (prio 1, conditional) |
| `versionCheckTask` | `src/sys/tasks.cpp:172` | FreeRTOS scheduler (prio 1) |
| `mergeOutputTimed()` | declared `src/sys/tasks.h:13` (defined `src/core/merge_engine.cpp:122`) | `dmxFrameTick` (`src/sys/tasks.cpp:135`) |
| `updateLedFromNet()` | `src/sys/tasks.cpp:87` | `ledTask` (`src/sys/tasks.cpp:159`) |
| `renderDisplay()` | `src/sys/tasks.cpp:93` | `displayTask` (`src/sys/tasks.cpp:167`) |

## 8. Data Flow

1. **Task spawn** (core 0, `setup()`): `createTasks()` creates five tasks; `dmxTxTask` pinned to core 1 prio 19, `netRxTask` pinned to core 0 prio 5 (`src/sys/tasks.cpp:78-85`).
2. **Network receive** (core 0, `netRxTask`): `artRdmPollRx()` drains ≤8 Art-Net packets into the RX ring (`src/sys/tasks.cpp:149`); `artPktDispatchAll()` dispatches enqueued packets (`src/sys/tasks.cpp:150`); `readSacn()` drains ≤4 sACN sockets and dispatches (`src/sys/tasks.cpp:151`). Frame writes go through the seqlock into `dmxBuffers[i].data[]` ([core-dmx-buffer](./core-dmx-buffer.md)).
3. **ArtSync commit** (core 0): `flushArtSyncStaged()` — if `syncMode` is active and `ARSTYSYNC_TIMEOUT_MS` (1 000 ms, `src/net/artnet.h:74`) has elapsed since `syncLastMs`, it logs a fallback, clears `syncMode`, and calls `commitArtSyncStaged()` (`src/sys/tasks.cpp:110-118`). This runs inside `dmxFrameTick`, so the commit is driven by the core-1 tick — see [Limitations](#17-limitations).
4. **Frame tick** (core 1, `dmxTxTask`): every 1 ms `vTaskDelayUntil` wakes; `dmxFrameTick()` calls `flushArtSyncStaged()` then `sceneFadeStep()` (`src/sys/tasks.cpp:124-125`).
5. **Per-output transmit**: for each `i` in `[0, MAX_OUTPUTS)`: if `cfg.outputs[i].enabled && outReady[i]` (`src/sys/tasks.cpp:128`), and the per-output period (`dmxPeriodMs(i)`, `src/sys/tasks.cpp:130`) has elapsed, call `snapshotAndTransmit(i)` (`src/sys/tasks.cpp:131`). If any output uses `TXSTYLE_DELTA`, skip `mergeOutputTimed()` (`src/sys/tasks.cpp:129,135`).
6. **Snapshot + kick** (`snapshotAndTransmit`): skip if `!outReady[outIdx]` (`src/sys/tasks.cpp:97`); skip if `txStyle == TXSTYLE_DELTA && !stats().outSrcLost[outIdx]` — i.e. only transmit on change when in delta mode and the source is still alive (`src/sys/tasks.cpp:98`); snapshot 513 bytes via `dmxBufSnapshot` (`src/sys/tasks.cpp:100`); if the RMT is idle (`rmtDmxIdle`, `src/sys/tasks.cpp:102`), kick the frame (`rmtDmxKick`) and bump stats (`src/sys/tasks.cpp:103-106`).

## 9. Protocol Layout

N/A (no wire protocol). The tasks module is the scheduler glue; it does not define or parse any packet format. The Art-Net wire format is documented in [net-artnet-protocol](./net-artnet-protocol.md), sACN in [net-sacn-protocol](./net-sacn-protocol.md), DMX in [drv-dmx-rmt-tx](./drv-dmx-rmt-tx.md), and RDM in [core-rdm-engine](./core-rdm-engine.md).

## 10. Config Integration

Reads `cfg.outputs[i]` fields (all from `src/cfg/config_schema.cpp` OUTPUT_FIELDS):

| Field | CFG flag | Schema line | Read in (tasks.cpp) |
|---|---|---|---|
| `txRate` | `CFG_LIVE` | `src/cfg/config_schema.cpp:168` (OENUM) | `dmxPeriodMs` (line 29) — selects `DMX_RATE_MS[]` index |
| `txStyle` | `CFG_LIVE` | `src/cfg/config_schema.cpp:169` (OENUM) | `snapshotAndTransmit` (line 98), `dmxFrameTick` (line 129) |
| `enabled` | `CFG_REBOOT` | `src/cfg/config_schema.cpp:154` (OBOOL) | `dmxFrameTick` (line 128) |

`txRate` and `txStyle` apply live (`CFG_LIVE`); changing them via the web UI or serial console takes effect on the next tick without reboot. `enabled` requires reboot (`CFG_REBOOT`). Writes: none — this module never writes config fields.

## 11. Lifecycle

- **Init (core 0, `setup()`)**: `cfgcore::load()` → `sanitizeOutputs()` → `dmxInitGuardBegin()` → `outputInitAll()` → `dmxInitGuardEnd()` → `createTasks()` (`src/main.cpp:46-130`).
- **Periodic — core 1**: `dmxTxTask` entry at `src/sys/tasks.cpp:138`; body is `dmxFrameTick()` + `vTaskDelayUntil(&xLastWakeTime, 1)` (`src/sys/tasks.cpp:141-142`). 1 ms cadence.
- **Periodic — core 0**: `netRxTask` entry at `src/sys/tasks.cpp:146`; drains net + dispatches every 2 ms (`src/sys/tasks.cpp:153`).
- **Periodic — prio 1**: `ledTask` (50 ms, `src/sys/tasks.cpp:161`), `displayTask` (200 ms, conditional, `src/sys/tasks.cpp:168`), `versionCheckTask` (60 000 ms, `src/sys/tasks.cpp:175`).
- **Shutdown**: None — tasks are never deleted; no cleanup hooks. Reboot is the only "shutdown" path.

## 12. Error Handling

- `dmxPeriodMs`: out-of-range `txRate` is clamped to index 0 (25 ms) (`src/sys/tasks.cpp:30`); the fallback is silent.
- `snapshotAndTransmit`: `!outReady[outIdx]` returns early (`src/sys/tasks.cpp:97`); snapshot failure returns early (`src/sys/tasks.cpp:100`); `rmtDmxIdle` false returns without transmitting (`src/sys/tasks.cpp:102`) — the frame is simply skipped that tick (benign, next 1 ms retry).
- `flushArtSyncStaged`: logs `"[ART] ArtSync timeout, falling back to immediate"` on timeout (`src/sys/tasks.cpp:114`); otherwise silent.
- `dmxInitGuardEnd`: if the NVS value after the 3 s wait does not match the written `counter + 1` (i.e. a crash reset it), the counter is left incremented and the function logs only on the stable-reset path (`src/sys/tasks.cpp:69-74`).
- No `esp_err_t` returns in tasks.cpp — all functions are `void`; transport-layer errors propagate as skipped frames.

## 13. Memory Allocation

- `DMX_RATE_MS[]` — `static const uint8_t` ROM array (5 bytes), `src/sys/tasks.cpp:25`.
- `s_crashCount`, `PREF_NS_TASKS`, `DMX_GUARD_TTL_MS` — static file-scope (`src/sys/tasks.cpp:39-41`).
- `frame[DMX_PACKET_SIZE]` (513 bytes) — stack-local in `snapshotAndTransmit` (`src/sys/tasks.cpp:99`). Lives 1 ms on the core-1 task stack (8192 bytes allocated at `src/sys/tasks.cpp:83`).
- Task stacks: `dmxTxTask` = 8192 B (`src/sys/tasks.cpp:83`), `netRxTask` = 8192 B (`src/sys/tasks.cpp:84`), `ledTask` = 2048 B (`src/sys/tasks.cpp:79`), `displayTask` = 4096 B (`src/sys/tasks.cpp:80`), `versionCheckTask` = 12288 B (`src/sys/tasks.cpp:81`).
- No heap allocation in tasks.cpp. NVS usage is transient (open/close per call) for the crash counter (`src/sys/tasks.cpp:44-47,58-61,66-68,70-73`).

## 14. Timing

| Item | Value | Source |
|---|---|---|
| `dmxTxTask` period | 1 ms | `src/sys/tasks.cpp:142` (`vTaskDelayUntil` tick 1) |
| `netRxTask` period | 2 ms | `src/sys/tasks.cpp:153` (`vTaskDelay(2)`) |
| `ledTask` period | 50 ms | `src/sys/tasks.cpp:161` |
| `displayTask` period | 200 ms | `src/sys/tasks.cpp:168` |
| `versionCheckTask` period | 60 000 ms | `src/sys/tasks.cpp:175` |
| DMX rate index 0 | 40 fps / 25 ms | `src/sys/tasks.cpp:25` (`DMX_RATE_MS[0]`) |
| DMX rate index 1 | 41.7 fps / 24 ms | `src/sys/tasks.cpp:25` |
| DMX rate index 2 | 33.3 fps / 30 ms | `src/sys/tasks.cpp:25` |
| DMX rate index 3 | 25 fps / 40 ms | `src/sys/tasks.cpp:25` |
| DMX rate index 4 | 20 fps / 50 ms | `src/sys/tasks.cpp:25` |
| ArtSync timeout | 1000 ms | `src/net/artnet.h:74` (`ARTSYNC_TIMEOUT_MS`) |
| Crash guard stable window | 3000 ms | `src/sys/tasks.cpp:41` (`DMX_GUARD_TTL_MS`) |
| `dmxTxTask` priority | 19 (highest) | `src/sys/tasks.cpp:83` |
| `netRxTask` priority | 5 | `src/sys/tasks.cpp:84` |

The 1 ms `dmxTxTask` tick is the hard real-time spine of the system — all DMX merge/transmit and scene fades happen within it.

## 15. Traceability

| Claim | Evidence |
|---|---|
| DMX rate table is {25,24,30,40,50} ms for indices 0–4 | `src/sys/tasks.cpp:25` |
| `DMX_RATE_COUNT` = sizeof array | `src/sys/tasks.cpp:26` |
| `dmxPeriodMs` reads `cfg.outputs[out].txRate` and clamps | `src/sys/tasks.cpp:29-31` |
| `dmxRateHz` = 1000 / `dmxPeriodMs` | `src/sys/tasks.cpp:34-36` |
| Crash counter namespace is `"dmxgw"` | `src/sys/tasks.cpp:40` |
| Guard TTL is 3000 ms | `src/sys/tasks.cpp:41` |
| `dmxInitGuardBegin` reads NVS `dmxcrash` | `src/sys/tasks.cpp:46` |
| Progressive disable loop: `i = crashCount-1` down to 0 | `src/sys/tasks.cpp:50-53` |
| `dmxInitGuardEnd` writes `crashCount+1` then waits 3 s | `src/sys/tasks.cpp:60,62-65` |
| Stable boot resets counter to 0 | `src/sys/tasks.cpp:69-75` |
| `dmxTxTask` pinned to core 1, prio 19, stack 8192 | `src/sys/tasks.cpp:83` |
| `netRxTask` pinned to core 0, prio 5, stack 8192 | `src/sys/tasks.cpp:84` |
| `ledTask`: stack 2048, prio 1, unpinned | `src/sys/tasks.cpp:79` |
| `displayTask`: stack 4096, prio 1, guarded by `dispReady` | `src/sys/tasks.cpp:80` |
| `versionCheckTask`: stack 12288, prio 1 | `src/sys/tasks.cpp:81` |
| `updateLedFromNet` sets rgb=0x000a00 (green), on=true | `src/sys/tasks.cpp:89-90` |
| `renderDisplay` is an empty stub | `src/sys/tasks.cpp:93-94` |
| `snapshotAndTransmit` checks `outReady` then delta/sourcelost | `src/sys/tasks.cpp:97-98` |
| `snapshotAndTransmit` snapshots 513 bytes, checks `rmtDmxIdle`, kicks | `src/sys/tasks.cpp:99-103` |
| `flushArtSyncStaged` checks `artNet().syncMode` | `src/sys/tasks.cpp:111` |
| `flushArtSyncStaged` uses `ARTSYNC_TIMEOUT_MS` | `src/sys/tasks.cpp:113`; value at `src/net/artnet.h:74` |
| `dmxFrameTick` calls `flushArtSyncStaged` + `sceneFadeStep` | `src/sys/tasks.cpp:124-125` |
| `dmxFrameTick` gate: `cfg.outputs[i].enabled && outReady[i]` | `src/sys/tasks.cpp:128` |
| `dmxFrameTick` delta flag: `txStyle == TXSTYLE_DELTA` | `src/sys/tasks.cpp:129` |
| `dmxFrameTick` period check: `now - outLastDmxMs[i] >= dmxPeriodMs(i)` | `src/sys/tasks.cpp:130` |
| `dmxFrameTick` skips `mergeOutputTimed` if any delta | `src/sys/tasks.cpp:135` |
| `dmxTxTask` loop: `dmxFrameTick` + `vTaskDelayUntil(&xLastWakeTime, 1)` | `src/sys/tasks.cpp:138-143` |
| `netRxTask` calls `artRdmPollRx`, `artPktDispatchAll`, `readSacn`, `artRdmDrainResponses` | `src/sys/tasks.cpp:149-152` |
| `netRxTask` delays 2 ms | `src/sys/tasks.cpp:153` |
| `ledTask` calls `updateLedFromNet` + `setLedColor`, 50 ms | `src/sys/tasks.cpp:157-162` |
| `displayTask` calls `renderDisplay` if `dispReady`, 200 ms | `src/sys/tasks.cpp:165-169` |
| `versionCheckTask` calls `versionCheck`, 60 000 ms | `src/sys/tasks.cpp:172-176` |
| `txRate` schema: OENUM (CFG_LIVE) | `src/cfg/config_schema.cpp:168` |
| `txStyle` schema: OENUM (CFG_LIVE) | `src/cfg/config_schema.cpp:169` |
| `enabled` schema: OBOOL (CFG_REBOOT) | `src/cfg/config_schema.cpp:154` |

## 16. Cross-References

- `[core-output-init](./core-output-init.md)` — provides `g_outputs[]`, `outReady[]`, `dmxInitGuardBegin/End`, `outputInitAll`; `dmxTxTask` reads `outReady[i]` (`src/sys/tasks.cpp:97,128`) and `g_outputs[i].rmt` (`src/sys/tasks.cpp:101`).
- `[core-dmx-buffer](./core-dmx-buffer.md)` — `dmxBufSnapshot(outIdx, frame)` (`src/sys/tasks.cpp:100`) reads the seqlock-staged `dmxBuffers[i].data[]`.
- `[drv-dmx-rmt-tx](./drv-dmx-rmt-tx.md)` — `rmtDmxIdle(rd)` / `rmtDmxKick(rd, frame, DMX_PACKET_SIZE)` (`src/sys/tasks.cpp:102-103`) drive the RMT peripheral.
- `[core-merge-engine](./core-merge-engine.md)` — `mergeOutputTimed()` (`src/sys/tasks.cpp:135`) blends timed merge when no output is in delta mode.
- `[core-scene-engine](./core-scene-engine.md)` — `sceneFadeStep()` (`src/sys/tasks.cpp:125`) advances scene fades each tick.
- `[core-stats](./core-stats.md)` — `stats()` updates `outSrcLost`, `outLastDmxMs`, `txFrames`, `outFps` (`src/sys/tasks.cpp:104-106`).
- `[net-artnet-protocol](./net-artnet-protocol.md)` — `artNet().syncMode`, `artNet().syncLastMs`, `commitArtSyncStaged()`, `artRdmPollRx`, `artPktDispatchAll`, `artRdmDrainResponses` used in `netRxTask` and `flushArtSyncStaged`.
- `[net-sacn-protocol](./net-sacn-protocol.md)` — `readSacn()` called in `netRxTask` (`src/sys/tasks.cpp:151`).
- `[sys-led-status](./sys-led-status.md)` — `updateLedFromNet` calls `setLedColor` (`src/sys/tasks.cpp:160`); `initLed` is called from `setup()` (`src/main.cpp:53`).
- `[sys-firmware-version](./sys-firmware-version.md)` — `versionCheck()` called by `versionCheckTask` (`src/sys/tasks.cpp:174`).
- `[sys-soak-monitor](./sys-soak-monitor.md)` — `soakInit()` called from `setup()` (`src/main.cpp:112`); `soakStatsJson()` consumed by `/diag/soak-stats` (`src/net/web_server.cpp:48`).
- `config-engine` — `cfgcore::load()` populates `cfg.outputs[].txRate/txStyle/enabled` before tasks run (`src/main.cpp:46`).

## 17. Limitations

- `flushArtSyncStaged()` (ArtSync commit logic) runs on **core 1** inside `dmxFrameTick`, but it reads `artNet().syncMode` / `syncLastMs` (written by `netRxTask` on core 0) with no seqlock or explicit synchronization — the read is of plain `uint32_t`/`uint8_t` members (`src/net/artnet.h:49-50`), which is benign on the ESP32-S3 32-bit load/store model but technically a data race by the C++ memory model.
- `g_dmxTask` (the task handle) is captured at `src/sys/tasks.cpp:83` but never used for `vTaskNotifyGiveFromISR`, `vTaskResume`, or priority changes in the inspected source — its only consumer is `main.cpp`-style external callers not present here (see [Open Questions](#18-open-questions)).
- `renderDisplay()` in tasks.cpp (`src/sys/tasks.cpp:93`) is an **empty stub**; the real display rendering lives in `src/sys/display.cpp` (`renderDisplay` is not defined there either — see [Open Questions](#18-open-questions)).
- The 1 ms `dmxTxTask` period includes the ArtSync timeout check and scene step on every tick even when ArtSync is inactive — minor wasted work, but no deadline violation at priority 19.
- `outSrcLost[outIdx]` (`src/sys/tasks.cpp:98`) is read without an explicit memory barrier, but it is written by `mergeOutput` on core 0 ([core-merge-engine](./core-merge-engine.md)) and read on core 1 — the `volatile` qualifier on `outSrcLost` (`src/core/stats.h:33`) provides compiler ordering; a hardware barrier is not used.

## 18. Open Questions

1. Not determinable from the inspected source code — what is the intended use of the captured `g_dmxTask` handle (`src/sys/tasks.h:5`). No `vTask*` call consuming it was found in `tasks.cpp`.
2. Not determinable from the inspected source code — where the real `renderDisplay` implementation lives. `src/sys/tasks.cpp:93` is an empty stub; `src/sys/display.cpp` is not in the inspected file set and the display entry point `initDisplay`/`dispReady` are declared in `src/sys/display.h:6-7` but their bodies were not inspected.
3. Not determinable from the inspected source code — whether the ArtSync `syncMode`/`syncLastMs` read in `flushArtSyncStaged` (`src/sys/tasks.cpp:111-112`) is intentionally unsynchronized or should be seqlock-guarded alongside the DMX buffer.
4. Not determinable from the inspected source code — the exact producer that sets `artNet().syncMode = true` and `syncLastMs`; likely in `src/net/artnet.cpp` ArtSync handler, not inspected.
5. Not determinable from the inspected source code — whether `dmxRateHz` (declared `src/sys/tasks.h:8`) is consumed anywhere; a grep for callers outside `tasks.cpp` was not performed against the full tree.

## 19. Testing

- No host-native test covers `tasks.cpp` — the task spawning, `dmxFrameTick`, `snapshotAndTransmit`, and the crash guard all require FreeRTOS, RMT, and seqlock runtime state that the `test/native/shim/` shims do not provide.
- `dmxPeriodMs` / `dmxRateHz` / `DMX_RATE_MS` are pure-ish (read `cfg`) but ununit-tested — no `test/native/*_test.cpp` references `DMX_RATE_MS` or `dmxPeriodMs`.
- `config_test.cpp` (host) tests config resolution and LED type presets (`test/native/config_test.cpp:24,54`) but does not exercise the task layer.
- The 1 ms `dmxTxTask` cadence and 2 ms `netRxTask` cadence are validated via the 5-minute firmware evaluation workflow in `CLAUDE.md` ("Firmware Evaluation Workflow"), which monitors serial output for `"[DMX] tx task running on core 1"` and `"[NET] rx task running on core 0"`.
- The crash guard is exercised by inducing a panic during `outputInitAll` and observing progressive output disablement across reboots — a manual hardware test, documented in `[core-output-init](./core-output-init.md):19`.
- `versionCheckTask` (GitHub release poll) is validated by observing the `/info` web endpoint's `version`/`board` fields (`src/net/web_routes.cpp:88-101`).

## 20. History

- Core-pinned task creation: `dmxTxTask` pinned to core 1 / `netRxTask` pinned to core 0 with `xTaskCreatePinnedToCore` (`src/sys/tasks.cpp:83-84`) — driven by issue #64 (RMT TX corruption under WiFi DMA contention, fixed by moving RDM to core 1).
- ArtSync staged-commit timeout added in `flushArtSyncStaged` (`src/sys/tasks.cpp:110`) using `ARTSYNC_TIMEOUT_MS` from `src/net/artnet.h:74`.
- `versionCheckTask` added with a 12 288-byte stack (`src/sys/tasks.cpp:81`) to accommodate the HTTPClient + JSON string parsing in `versionCheck` ([sys-firmware-version](./sys-firmware-version.md)).
- Crash guard (`dmxInitGuardBegin`/`End`) moved into `tasks.cpp` from the output-init module during the 5-layer split; the NVS namespace is `"dmxgw"` (`src/sys/tasks.cpp:40`).
- 5-LED panel constants (`DMX_GUARD_TTL_MS`, `DMX_RATE_MS`) are `static const` file-scope to avoid header pollution — see [sys-led-status](./sys-led-status.md) for the panel layout consumed by `ledTask`.
- `renderDisplay` left as an empty stub at `src/sys/tasks.cpp:93` pending the display driver migration from the v1 monolith.
