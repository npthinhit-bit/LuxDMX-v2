# Task Scheduling Specification

Domain: sys.tasks

## 1. Module Overview

The Tasks module owns the FreeRTOS task lifecycle for the entire firmware. It creates all RTOS tasks with explicit core pinning, runs the two core-pinned data-path tasks (`dmxTxTask` on core 1, `netRxTask` on core 0), hosts the low-priority housekeeping tasks (LED, display, version check), drives the per-tick DMX frame engine, and implements the NVS-backed crash guard that prevents a bad DMX output pin from bricking the device.

The module is the convergence point of the system's real-time requirements: the 1 ms `dmxTxTask` tick is the hard real-time spine, and all DMX merge, scene fades, and ArtSync commit happen within it. The 2 ms `netRxTask` tick is the network ingress path. The crash guard wraps the entire output initialization sequence to ensure system survivability across reboot cycles.

## 2. External Interfaces

### Caller-Facing API

| Function | Visibility | Purpose |
|---|---|---|
| `createTasks` | Public (called from setup) | Creates all five FreeRTOS tasks with core pinning |
| `dmxInitGuardBegin` | Public (called from setup, before output init) | Reads NVS crash counter; disables lowest-indexed outputs if recovering from a crash |
| `dmxInitGuardEnd` | Public (called from setup, after output init) | Writes incremented counter, waits 3 s for stability, resets counter on survival |
| `dmxPeriodMs` | Internal (used by `dmxTxTask`) | Maps output index to TX period in milliseconds |
| `dmxRateHz` | Internal | Computes output rate in Hz from the period |
| `updateLedFromNet` | Internal (called by `ledTask`) | Sets LED color from network state |
| `renderDisplay` | Stub (called by `displayTask`) | Empty placeholder; real implementation in a separate display module |

### Configuration Rate Table

| Index | Period (ms) | Rate (fps) |
|---|---|---|
| 0 | 25 | 40 |
| 1 | 24 | ~41.7 |
| 2 | 30 | ~33.3 |
| 3 | 40 | 25 |
| 4 | 50 | 20 |

Indexed by the `txRate` config field on each output. Indices are clamped to the valid range.

### Task Table

| Task | Core | Priority | Stack | Period |
|---|---|---|---|---|
| `dmxTxTask` | Core 1 | 19 (highest) | 8192 | 1 ms |
| `netRxTask` | Core 0 | 5 | 8192 | 2 ms |
| `ledTask` | Any | 1 | 2048 | 50 ms |
| `displayTask` | Any | 1 | 4096 | 200 ms (conditional) |
| `versionCheckTask` | Any | 1 | 12288 | 60 s |

### NVS Crash Counter

| Key | Namespace | Type | Default | Reset value |
|---|---|---|---|---|
| `dmxcrash` | `"dmxgw"` | `uint8` | 0 (no crash) | 0 (on stable boot) |

### External Globals Read by Task Layer

| Symbol | Owner | Read by | Purpose |
|---|---|---|---|
| `cfg.outputs[i].txRate` | Config engine | `dmxTxTask` | Selects DMX frame period |
| `cfg.outputs[i].txStyle` | Config engine | `dmxTxTask` | Delta vs. continuous transmit |
| `cfg.outputs[i].enabled` | Config engine | `dmxTxTask` | Output enable gate |
| `outReady[i]` | Output init | `dmxTxTask` | Per-output RMT init success |
| `g_outputs[i].rmt` | Output init | `dmxTxTask` | RMT channel handle per output |
| `artNet().syncMode` | Art-Net protocol | Core 1 (via tick) | ArtSync staged-commit state |
| `artNet().syncLastMs` | Art-Net protocol | Core 1 (via tick) | ArtSync commit deadline |

## 3. State Machine

### Task Scheduler State

The task scheduler itself has no state machine — tasks are created once at startup and run for the lifetime of the system. No task is ever deleted; reboot is the only "shutdown" path.

### Crash Guard State Machine (3 states, NVS-backed)

```
        counter==0                  counter>0
  +----------+        +------------v------------+
  |   BOOT   |------->|   CRASH_RECOVERY        |
  | (read NVS)       | (disable outputs        |
  +----------+       |  [0..counter-1])        |
        | counter>0  +-----+-----------------+
        |                 | write counter+1
        v                 v
  +----------+     +----------+
  | INIT_OK  |     | WAIT_3S  |
  | (no dis) |     | (spin    |
  +----+-----+     | 3000ms)  |
       | survives 3s?  +---+----+
       |               |    |     |
       v              YES   v    NO
  +----------+     +----------+   (counter stays >0,
  | STABLE   |     | STABLE   |   next boot disables more)
  | _BOOT    |     | _BOOT    |
  | (counter=0)|    | (counter=0)|
  +----------+     +----------+
```

| Transition | Trigger | Action |
|---|---|---|
| BOOT -> INIT_OK | Counter == 0 at guard-begin | Proceed with all outputs enabled |
| BOOT -> CRASH_RECOVERY | Counter > 0 at guard-begin | Disable outputs `[0 .. counter-1]`, clamped by `MAX_OUTPUTS` |
| INIT_OK / CRASH_RECOVERY -> WAIT | Guard-end reached | Write `counter + 1` to NVS; begin 3 s wait |
| WAIT -> STABLE_BOOT | Survived 3 s (NVS value unchanged) | Reset counter to 0 |
| WAIT -> CRASH_RECOVERY (next boot) | Panic during the 3 s window | NVS value differs from written value; counter stays incremented |

### DMX Frame Tick State (in `dmxTxTask`)

Level-triggered per-tick sweep — no persistent state within the tick loop:

| Step | Condition | Action |
|---|---|---|
| 1. ArtSync flush | Always | Check ArtSync timeout; commit staged frames if 1000 ms elapsed |
| 2. Scene step | Always | Advance scene fade by one 1 ms step |
| 3. For each output | `enabled && outReady[i]` | Check per-output period; transmit if elapsed |
| 4. Rate gate | `now - lastTxMs[i] >= dmxPeriodMs(i)` | Snapshot seqlocked buffer, check RMT idle, kick frame |
| 5. Delta skip | `txStyle == DELTA && source alive` | Skip transmit (only send on change) |

## 4. Data Flow

1. **Task spawn (core 0, setup):** `createTasks()` creates five tasks. `dmxTxTask` is pinned to core 1 at priority 19. `netRxTask` is pinned to core 0 at priority 5.

2. **Network receive (core 0, `netRxTask`, 2 ms):**
   - `artRdmPollRx()` drains up to 8 Art-Net packets into the RX ring.
   - `artPktDispatchAll()` dispatches enqueued packets.
   - `readSacn()` drains sACN sockets (up to 4) and dispatches.
   - Frame writes go through the seqlock into the per-output `dmxBuffers[i].data[]`.

3. **ArtSync commit (core 1, inside tick):** If `syncMode` is active and 1000 ms has elapsed since `syncLastMs`, the staged ArtDMX frames are committed immediately (fallback on sync timeout).

4. **Frame tick (core 1, `dmxTxTask`, 1 ms):** `dmxFrameTick()` runs:
   - Flushes any timed-out ArtSync staging.
   - Steps all active scene fades by one interval.
   - For each enabled, ready output whose TX period has elapsed: snapshots the 513-byte seqlocked frame, checks if the RMT channel is idle, and kicks a new transmission.
   - If any output uses delta mode, the merge engine step is skipped (only changed slots are sent).
   - Stats are updated (frame count, last-TX timestamp, source-lost flags).

5. **Cross-core buffer handoff:** The seqlock-protected DMX buffer is the only cross-core data path. Core 0 writes frame data (via network handlers); core 1 reads via snapshot. The seqlock ensures the reader never sees a torn write.

## 5. Configuration Integration

| Config field | CFG flag | Read in | Live/Reboot |
|---|---|---|---|
| `txRate` | CFG_LIVE | `dmxPeriodMs` -> selects `DMX_RATE_MS[]` index | Live |
| `txStyle` | CFG_LIVE | `snapshotAndTransmit` (delta skip), `dmxFrameTick` | Live |
| `enabled` | CFG_REBOOT | `dmxFrameTick` (output gate) | Reboot |

Live fields (`txRate`, `txStyle`) take effect on the next 1 ms tick without reboot. The `enabled` field requires reboot because output initialization (RMT channel creation) happens at boot, not at runtime. The task layer never writes config fields — it only reads them. The crash guard may mutate `cfg.outputs[i].enabled` in RAM during boot (ephemerally; not persisted to NVS config).

## 6. Lifecycle

- **Init phase (core 0, setup):**
  1. Config load (`cfgcore::load()`) populates `cfg.outputs[i].enabled` and all output pin fields.
  2. `dmxInitGuardBegin()` reads the NVS crash counter.
  3. `outputInitAll()` initializes RMT, UART, and DE/RE GPIO for each enabled output.
  4. `dmxInitGuardEnd()` writes incremented counter, waits 3 s, resets on survival.
  5. Network bring-up, mDNS registration, web server, WebSocket init.
  6. `createTasks()` spawns all five tasks.

- **Periodic — core 1:** `dmxTxTask` body is `dmxFrameTick()` + `vTaskDelayUntil` (1 ms cadence). All DMX merge, transmit, scene fades, and ArtSync commit happen here.

- **Periodic — core 0:** `netRxTask` entry; drains net sockets + dispatches every 2 ms.

- **Periodic — priority 1:** `ledTask` (50 ms), `displayTask` (200 ms, conditional on display readiness), `versionCheckTask` (60 s).

- **Shutdown:** None — tasks are never deleted. Reboot is the only shutdown path.

## 7. Error Handling

| Component | Error | Handling |
|---|---|---|
| `dmxPeriodMs` | Out-of-range `txRate` index | Clamped to index 0 (25 ms); silent fallback |
| `snapshotAndTransmit` | Output not ready (`!outReady[i]`) | Returns early, frame skipped |
| `snapshotAndTransmit` | Seqlock snapshot failure (8 retries) | Returns early, frame skipped (never transmitted corrupted) |
| `snapshotAndTransmit` | RMT still busy (`rmtDmxIdle` false) | Kick skipped, retried next 1 ms tick |
| `dmxFrameTick` | Output disabled or not ready | Skipped entirely |
| `flushArtSyncStaged` | ArtSync timeout (1000 ms) | Logs warning; commits staged frames immediately as fallback |
| `dmxInitGuardBegin` | NVS read failure | `Preferences::getUChar` defaults to 0 (treats as no crash) |
| `dmxInitGuardEnd` | NVS write failure | Verification read returns stale value; counter stays elevated (conservative: next boot disables more) |
| `dmxInitGuardEnd` | Panic during 3 s window | NVS value differs from written value; counter persists, next boot disables one more output |
| Crash counter overflow | Counter reaches 255 | Wraps to 0 on next increment; counter resets to "no crash" state (theoretical only — would require ~7.6 h of continuous reboot cycling at 3 s/boot) |

All task-layer functions return `void` — transport-layer errors propagate as skipped frames, not as return codes.

## 8. Timing Constraints

| Path | Period | Deadline |
|---|---|---|
| `dmxTxTask` tick | 1 ms | Hard real-time; priority 19 on core 1 |
| `netRxTask` tick | 2 ms | Bounded UDP drain (8 Art-Net, 4 sACN per call) |
| ArtSync timeout | 1000 ms | Fallback commit of staged frames |
| sACN Stream Sync timeout | 500 ms | Sync loss commit of staged frames |
| RDM TX to RX turnaround | ~3 ms | 9 ms response window |
| RDM response timeout | 9 ms | UART RX window on core 1 |
| RDM discovery | 8 s budget total | Binary search cap |
| Crash guard stability window | 3000 ms | Hard boot-time delay after output init |
| LED task | 50 ms | Status update |
| Display task | 200 ms | Status update (conditional) |
| Version check | 60 000 ms | Background firmware check |
| DMX frame air time | ~24.3 ms | 513 slots x 44 us + break + MAB |

The 1 ms `dmxTxTask` tick is the hard real-time spine — all DMX merge/transmit, scene fades, and ArtSync commit occur within it. The 3-second crash guard stability window is a hard boot-time delay: `setup()` cannot proceed to task spawn until it elapses.

## 9. Memory & Allocation Model

- **Rate table:** `DMX_RATE_MS` — static const array (5 bytes of ROM).
- **Crash counter:** `s_crashCount` — static file-scope `uint8_t` in BSS (zero-initialized).
- **NVS namespace string:** `PREF_NS_TASKS` — static const char* in rodata.
- **Frame snapshot buffer:** 513 bytes stack-local in `snapshotAndTransmit` — lives for 1 ms on the `dmxTxTask` stack.
- **Task stacks:** `dmxTxTask` = 8192 B, `netRxTask` = 8192 B, `ledTask` = 2048 B, `displayTask` = 4096 B, `versionCheckTask` = 12288 B.
- **NVS usage:** Transient `Preferences` objects (open/close per guard call). NVS uses its own internal partition; untouched by task-layer stack objects.
- No heap allocation in the task layer. All ring buffers and global state are static/BSS.

## 10. Safety Considerations

- **Core isolation (critical):** AsyncTCP is pinned to core 0 with a 16 KB stack and queue depth 128 (via build flags). `dmxTxTask` runs on core 1 at priority 19 — the highest in the system. This ensures network/WiFi activity can never preempt the 1 ms DMX transmit tick. Measured on the HIL bench: with AsyncTCP on core 1, RDM TX-to-RX turnaround inflated 3-4x (median 15 to 53 us, max 126 us).
- **Seqlock for frame integrity:** The network-written DMX buffer is read by `dmxTxTask` via a seqlock with 8 retry attempts. Torn reads are detected and skipped — never transmitted as corrupted data. This is the cross-core safety boundary between core 0 (network) and core 1 (DMX TX).
- **Crash guard (brick prevention):** An NVS-backed counter progressively disables the lowest-indexed output on each consecutive crash during output initialization. If a specific output's pin or RMT channel causes a panic, the device boots with that output disabled on the next cycle, eventually isolating the faulty pin. A surviving 3-second window resets the counter, re-enabling all outputs.
- **Non-blocking idle check:** `rmtDmxIdle` uses a zero-timeout wait in the 1 ms tick, so a slow or oversized frame never blocks the tick — it is skipped and retried on the next cycle.
- **RDM task isolation:** The RDM task runs at priority 18 on core 1 (just below `dmxTxTask` at 19), ensuring RDM transactions can interrupt lower-priority work but never the 1 ms DMX tick.
- **DE/RE GPIO coordination:** The DE/RE pin stays HIGH (drive) between RDM transactions. `dmxTxTask` never calls the DE/RE control function — it only calls the RMT kick. DMX output is never interrupted during RDM listen windows.

## 11. Cross-Module Dependencies

```
                    +-----------+
                    | Config    |     cfg.outputs[i].txRate/txStyle/enabled
                    | Engine    |
                    +-----+-----+
                          |
                    +-----v-----+     outReady[i], g_outputs[i].rmt
                    | Output    |
                    | Init      |
                    +-----+-----+
                          |
    +----------+          |              +-----------+
    | Crash    |----------+-> outputInit  | RMT TX    |
    | Guard    |           |              | (drv)     |
    +----------+          |              +-----------+
                          |
    +----------+          |              +-----------+
    | DMX      |<---------+--snapshot--->| DMX Buffer|
    | Tasks    |  kick     |              | (core)    |
    | (sys)    |           |              +-----------+
    +----+-----+           |                  ^
         |                 |                  |
    +----v------+          |              +---+----+
    | Merge     |<---------+--timed----->| Scene   |
    | Engine    |                       | Engine  |
    | (core)    |                       | (core)  |
    +-----------+                       +---------+

    Core 0 side:                    Core 1 side:
    +---------------------+         +---------------------+
    | netRxTask (core 0)  |         | dmxTxTask (core 1)  |
    | - ArtNet drain      |---seqlock-->|
    | - sACN drain        |---frame--->  | - snapshot      |
    | - dispatch          |             | - merge         |
    | - responses         |---<----------| - kick RMT      |
    +---------------------+         +---------------------+
```

| Module | Consumes from task layer | Provides to task layer |
|---|---|---|
| Output Init | — | `outReady[]`, `g_outputs[].rmt` |
| DMX Buffer | — | `dmxBufSnapshot` (seqlock read) |
| DMX RMT TX | — | `rmtDmxIdle`, `rmtDmxKick` |
| Merge Engine | — | `mergeOutputTimed` |
| Scene Engine | — | `sceneFadeStep` |
| Stats | — | `stats()` (write frame count, source-lost) |
| Art-Net Protocol | — | `artNet().syncMode`, `syncLastMs`, `commitArtSyncStaged`, `artRdmPollRx`, `artPktDispatchAll`, `artRdmDrainResponses` |
| sACN Protocol | — | `readSacn` |
| LED Status | — | `initLed`, `setLedColor` |
| Firmware Version | — | `versionCheck` |
| Soak Monitor | — | `soakInit`, `soakStatsJson` |
| Config Engine | `cfg.outputs[]` fields | — |
| RDM Task | — | Shares core 1; RDM task priority 18 |
| Syslog | — | `syslogInit` |

## 12. Testing Verification

No host-native test coverage exists for the task layer. The task spawning, `dmxFrameTick`, `snapshotAndTransmit`, and the crash guard all require FreeRTOS, RMT, and seqlock runtime state that the native test shims do not provide. `dmxPeriodMs` / `dmxRateHz` / the rate table are pure-ish (read config) but are not unit tested.

The 1 ms `dmxTxTask` and 2 ms `netRxTask` cadences are validated via the 5-minute firmware evaluation workflow, which monitors serial output for task startup messages and heap stability. The crash guard is exercised by inducing a panic during output initialization and observing progressive output disablement across reboots — a manual hardware test. The `versionCheckTask` is validated by observing the `/info` web endpoint's version and board fields. The `config_test` host test covers config field resolution but does not exercise the task layer.

## 13. Open Questions

- The intended use of the captured `dmxTask` handle (for inter-task notification or priority manipulation) is not visible in the inspected source — no `vTask*` call consuming it was found.
- Where the real `renderDisplay` implementation lives: the function in the task layer is an empty stub, and the display module body was not fully inspected.
- Whether the ArtSync `syncMode` / `syncLastMs` read in the frame tick is intentionally unsynchronized (the read of these plain members from core 1 is a benign data race on the ESP32-S3 32-bit load/store model but technically violates the C++ memory model).
- The exact producer that sets `syncMode = true` and `syncLastMs` (likely in the Art-Net sync handler, not inspected).
- Whether `dmxRateHz` is consumed anywhere outside the task layer.
- Whether the 3-second crash guard window value is tunable per-board or hardcoded across all environments (no build-flag override was found).

## 14. History

- Core-pinned task creation: `dmxTxTask` pinned to core 1, `netRxTask` pinned to core 0, using `xTaskCreatePinnedToCore` — driven by issue #64 (RMT TX corruption under WiFi DMA contention, fixed by moving RDM to core 1).
- ArtSync staged-commit timeout added in the frame tick using the 1000 ms `ARSTYSYNC_TIMEOUT_MS` constant, providing a fallback when no ArtSync packet is received.
- `versionCheckTask` added with a 12288-byte stack to accommodate HTTP client plus JSON string parsing in the version check path.
- Crash guard (`dmxInitGuardBegin` / `dmxInitGuardEnd`) lives in the tasks module, having been moved there during the 5-layer refactor from the output-init module.
- Rate table, crash-guard TTL, and guard constants are declared as `static const` file-scope to avoid header pollution.
- `renderDisplay` left as an empty stub pending the display driver migration from the v1 monolith.
