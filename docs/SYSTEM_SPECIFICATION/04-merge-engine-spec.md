# Merge Engine - System Specification

## 1. Module Overview

**Module ID:** core.merge-engine
**Domain:** DMX frame merging from multiple network sources
**Layer:** core (consumes cfg, produces core DMX buffer)

Selects which network sender(s) contribute DMX slot data to each enabled output based on the configured merge mode and E1.31 source priority, then writes the merged result into the seqlock-protected per-output DMX buffer. Runs on both core 0 (per-packet routing path) and core 1 (1 ms task tick). Does not touch hardware directly; delegates scene recall to the scene engine and alerting to sys-alert.

### Merge Modes

| Mode | Enum | Behavior |
|---|---|---|
| Off | MERGE_OFF (0) | Last-seen live source wins the whole frame |
| HTP | MERGE_HTP (1) | Per-channel highest-takes-priorities across top-priority sources |
| LTP | MERGE_LTP (2) | Most recently seen top-priority source wins |
| LTP Takeover | MERGE_LTP_TAKEOVER (3) | Highest-priority source wins; ties broken by newest (lastMs) |
| Priority | MERGE_PRIORITY (4) | Only top-priority sources contribute, per-channel max |

### Loss Modes

| Mode | Enum | Behavior on source loss |
|---|---|---|
| Hold | LOSS_HOLD (0) | Buffer retains last frame |
| Zero | LOSS_ZERO (1) | Zero all 512 slots |
| Stop | LOSS_STOP (2) | No buffer write; transmit halt gated by outSrcLost[] + txStyle |
| Preset | LOSS_PRESET (3) | Recall a scene preset (index from lossPreset) |
| Home | LOSS_HOME (4) | Recall the home scene |

## 2. External Interfaces

### Entry Points

| Function | Caller | Trigger |
|---|---|---|
| mergeOutput(int outIdx) | routeFrameImpl() (core 0), commitArtSyncStaged() (core 0, ArtSync), dmxFrameTick() (core 1) | After a new frame is routed to an output |
| mergeOutputTimed() | dmxFrameTick() (core 1) | Every 1 ms tick when no output is in delta TX mode |
| portFailsafeMs(const DmxOutput&) (inline helper) | mergeOutput(), mergeOutputTimed() | Computes effective source-loss timeout |

### Input Data (read-only)

- cfg.outputs[outIdx] (include/config_schema.h:38-48): enabled, mergeMode, lossMode, lossPreset, failsafeTimeout
- SenderTracker.senders[] (src/core/sender_tracker.h:13-24): each sender ip, universe, priority, data, dataLen, lastMs
- portAddress(const DmxOutput&) (src/core/frame_router.h:8): 15-bit output address for universe matching

### Output Data (written)

| Target | What |
|---|---|
| dmxBufferState().buffers[outIdx].data[1..512] | Merged DMX frame bytes |
| stats().outSrcLost[outIdx] | Source-loss flag (true when no contributors) |
| stats().rxLossCount[outIdx] | Increments on each tick with loss |
| stats().srcStatus | Updated via sourceStatus() in frame_router |

## 3. State Machine

mergeOutput() is stateless - it computes the merged frame from the current sender table and writes it. The implicit state machine lives in the caller:

- **Source Active** (>=1 contributor within failsafeTimeout): merge according to mergeMode and write buffer; clear outSrcLost[outIdx]; call alertSourceRestored on transition.
- **Source Lost** (0 contributors): set outSrcLost[outIdx] = true; increment rxLossCount[outIdx]; call alertSourceLost on transition; apply lossMode branch:
  - LOSS_ZERO - write 512 zero bytes into buffer
  - LOSS_PRESET - delegate to scene engine sceneRecall(lossPreset)
  - LOSS_HOME - delegate to scene engine sceneRecallHome()
  - LOSS_STOP - no buffer write (transmit halt handled by task)
  - LOSS_HOLD - no buffer write (retain last frame)

## 4. Data Flow

1. **Sender update** (core 0, on packet arrival): updateSender() caches new frame data, priority, lastMs, and universe into the sender table.
2. **Route frame** (core 0): routeFrameImpl() calls mergeOutput(i) for each output whose 15-bit address matches the sender universe.
3. **Task-driven timed merge** (core 1): dmxFrameTick() calls mergeOutputTimed() when no output uses delta TX style. It scans all senders for each enabled output; if a sender has exceeded failsafeTimeout, it calls mergeOutput(i).
4. **Contributor selection**: Iterate MAX_SENDERS (16), filter by active sender (ip != 0) and universe match (universe == portAddress(out)), then filter by recency (now - lastMs < failsafeTimeout). If zero contributors -> loss handling branch.
5. **Priority filtering**: Compute topPrio from contributing senders. Only senders with priority >= topPrio participate.
6. **Merge dispatch**:
   - MERGE_LTP_TAKEOVER - highest-priority sender wins; tie-break on lastMs
   - MERGE_PRIORITY - per-channel max across top-priority senders (256 slots)
   - MERGE_HTP with >=2 sources - per-channel max across top-priority senders (512 slots)
   - Default (MERGE_OFF, MERGE_LTP, single source) - newest top-priority sender wins the whole frame
7. **Buffer write**: Merged frame is memcpy'd into buffers[outIdx].data[1..512], bracketed by dmxBufWriteBegin/WriteEnd seqlock on both core 0 and core 1 paths.

## 5. Configuration Integration

All fields read from the Config struct (include/config_schema.h:38-48), no writes. All are CFG_LIVE - new values take effect on the next mergeOutput() / mergeOutputTimed() call without reboot.

| Field | CFG flag | Default | Read in |
|---|---|---|---|
| outputs[i].enabled | CFG_LIVE | board template | mergeOutputTimed() |
| outputs[i].mergeMode | CFG_LIVE | board template | mergeOutput() |
| outputs[i].lossMode | CFG_LIVE | board template | mergeOutput() |
| outputs[i].lossPreset | CFG_LIVE | board template | mergeOutput() |
| outputs[i].failsafeTimeout | CFG_LIVE | board template | portFailsafeMs() |

Pin/GPIO settings (txPin, rxPin, rtsPin) are CFG_REBOOT and are not read by this module.

## 6. Lifecycle

- **Init**: No explicit init function. Reads cfg and sender table state at call time.
- **Per-frame hook**: mergeOutput(int outIdx) called immediately after updateSender() caches a new frame.
- **Per-tick hook**: mergeOutputTimed() called from dmxFrameTick() - only when no output is in TXSTYLE_DELTA mode (delta outputs are merged on-demand per-packet).
- **Shutdown**: None. Module holds no resources.

## 7. Error Handling

- mergeOutput() returns void - no error return. Failures are silent.
- portFailsafeMs() returns SOURCE_TIMEOUT_MS (2500 ms) when failsafeTimeout is 0 - neutral-value fallback.
- LOSS_PRESET calls sceneRecall() which is a no-op if the scene table is not loaded; the buffer retains its last value.
- alertSourceLost / alertSourceRestored do not check return values - webhook failure is non-fatal.
- outIdx bounds checking is the caller responsibility.

## 8. Timing Constraints

- mergeOutput() scans up to MAX_SENDERS (16) senders per call. O(16) comparisons + O(1) memcpy of 512 or 256 bytes.
- mergeOutputTimed() iterates all MAX_OUTPUTS (4) x MAX_SENDERS (16) = 64 comparisons per 1 ms tick on core 1.
- portFailsafeMs() converts failsafeTimeout (seconds->ms). SOURCE_TIMEOUT_MS = 2500 ms.
- Source-loss detection relies on millis() time source (FreeRTOS-safe).
- Best-effort, no hard deadline - completes well within the 1 ms DMX tick period.
- mergeOutput() executes on core 0 (routeArtNet path) or core 1 (dmxFrameTick); seqlock write makes buffer mutation safe across both cores.

## 9. Memory & Allocation Model

- **No heap allocation** anywhere in this module.
- merged[256] - stack temporary for MERGE_PRIORITY (256 bytes; E1.31 priority is 256-slot max).
- merged[512] - stack temporary for MERGE_HTP (512 bytes).
- contrib[MAX_SENDERS] - stack index array (64 bytes for 16 x int).
- g_senderTracker - static global; senders[MAX_SENDERS] array (~9.4 KB) lives in DRAM.
- Total per-call stack: ~576 bytes (largest path: merged[512] + contrib[64]).

## 10. Safety Considerations

- **Seqlock protection**: All buffer writes are bracketed by dmxBufWriteBegin/WriteEnd. The core-1 reader (dmxBufSnapshot) retries if it observes an odd seqlock ticket, preventing torn reads.
- **Crash-safe**: No direct hardware interaction; buffer writes go through the seqlock which the transmit task reads atomically.
- **Source-loss fail-safe**: LOSS_HOLD (default safe behavior) retains the last frame rather than transmitting zeros or corrupt data. LOSS_ZERO provides explicit blackout. LOSS_STOP defers to the transmit task for a clean halt.
- **Timeout cap**: SOURCE_TIMEOUT_MS default of 2500 ms prevents stale frames from being merged indefinitely.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| core.frame-router | calls mergeOutput() | Invokes merge after routing each frame |
| core.sender-tracker | read | Provides SenderTracker and Sender structs |
| core.dmx-buffer | write | Seqlock-protected DmxBufferState receives merged data |
| core.scene-engine | called | sceneRecall() / sceneRecallHome() on LOSS_PRESET / LOSS_HOME |
| core.stats | write | outSrcLost[], rxLossCount[], srcStatus |
| sys.tasks | calls mergeOutputTimed() | 1 ms tick-driven timed merge |
| sys.alert | calls | alertSourceLost / alertSourceRestored for webhook notifications |
| net.artnet | calls mergeOutput() | commitArtSyncStaged() post-commit |
| cfg (config_schema) | read | DmxOutput fields (enabled, mergeMode, lossMode, lossPreset, failsafeTimeout) |

## 12. Testing Verification

| Test Case | File:Line | Validates |
|---|---|---|
| HTP merge | test/native/merge_test.cpp:26-43 | MERGE_HTP per-channel max |
| OFF / last-frame-wins | test/native/merge_test.cpp:45-61 | MERGE_OFF last-seen wins |
| LOSS_ZERO | test/native/merge_test.cpp:63-76 | Zeros 512 slots on source loss |
| Cross-universe isolation | test/native/merge_test.cpp:78-92 | Only matching-universe senders contribute |
| MERGE_LTP_TAKEOVER | test/native/merge_test.cpp:94-116 | Highest priority, tie-break on lastMs |
| MERGE_PRIORITY | test/native/merge_test.cpp:118-136 | Per-channel max across top-priority senders |
| Failsafe timeout default | test/native/merge_test.cpp:138-153 | 2500 ms when failsafeTimeout is 0 |
| Failsafe timeout override | test/native/merge_test.cpp:155-169 | Per-output failsafeTimeout override |
| LOSS_PRESET fallback | test/native/merge_test.cpp:171-183 | Scene recall on LOSS_PRESET |
| LOSS_HOME fallback | test/native/merge_test.cpp:185-196 | Home scene recall on LOSS_HOME |

**Untested**: mergeOutputTimed() has no direct unit test - depends on millis() and sender table state, exercised only on hardware.

## 13. Open Questions

1. Whether alertSourceLost webhook POST is non-blocking (spawns a task vs. synchronous POST on the calling core).
2. Exact behavior of sceneRecall() when lossPreset is out of bounds.
3. Whether the merged[256] limit in MERGE_PRIORITY is a deliberate E1.31 spec alignment or a latent truncation bug.

## 14. History

- **Initial**: MERGE_OFF, MERGE_HTP, MERGE_LTP defined to match legacy LuxDMX behavior (include/config_enums.h:5).
- **E1.31 priority support**: MERGE_LTP_TAKEOVER and MERGE_PRIORITY added to allow high-priority controllers to preempt lower-priority live sources.
- **Scene integration**: LOSS_PRESET and LOSS_HOME added to invoke the scene engine on signal loss.
- **Transmit-task coupling**: LOSS_STOP path deferred to the transmit task outSrcLost check (sys/tasks.cpp:98).
- **Timed loss detection**: mergeOutputTimed() added to handle source-loss detection for non-delta outputs.

## Known Limitations

- **Dead ip pointer**: alertSourceLost is called with a null source-IP pointer, so webhook alerts cannot report the lost source IP address. Pre-existing code defect.
- **Priority asymmetry**: MERGE_PRIORITY operates on 256 slots while MERGE_HTP operates on 512 slots. This aligns with E1.31 priority semantics but is a subtle constraint.
- **Unsynchronized config reads**: The merge engine reads cfg without a lock. cfg is mutated only during config load or web-apply and is not expected to change mid-merge, but this is not explicitly synchronized.
