# Scene Engine - System Specification

## 1. Module Overview

**Module ID:** core.scene-engine
**Domain:** Preset scene storage, linear fade engine, and timecode-triggered playback
**Layer:** core (reads cfg, writes core DMX buffer via seqlock)

Stores up to 32 scene presets (8 on non-PSRAM targets), each holding a full per-output DMX frame of 4 x 513 bytes (start-code byte plus 512 slots per output). A per-output linear fade engine interpolates channel values from the current DMX frame toward the scene target over a caller-specified fade duration. Scenes may be recalled instantly (fadeMs = 0), triggered on incoming Art-Net TimeCode, armed as source-loss presets, or fired via WebSocket commands.

The module owns only scene state. It does not parse network packets, merge senders, or drive the RMT peripheral directly; it snapshots the current frame via the seqlock read API and commits interpolated output through the seqlock write API.

### Scene Capacity

| Target | Max Scenes | Scene Storage | Fade Storage |
|---|---|---|---|
| PSRAM-enabled | 32 | PSRAM (~67 KB) | Static DRAM (~4.2 KB) |
| Non-PSRAM | 8 | Internal heap (~17 KB) | Static DRAM (~4.2 KB) |

## 2. External Interfaces

### Entry Points

| Function | Caller | Trigger | Direction |
|---|---|---|---|
| sceneLoadAll() | Boot sequence | Device startup | Core 0 |
| sceneFadeStep() | DMX frame tick | Every 1 ms | Core 1 |
| sceneTriggerPlay(int idx, uint16_t fadeMs) | Art-Net ArtTrigger handler, WebSocket "scene" command, timecode trigger | External command | Command |
| sceneCheckTimecodeTrigger() | Art-Net TimeCode handler | Art-Net TimeCode frame received | Event |
| sceneSaveNvs(int idx) | WebSocket "saveScene" command | External command | Command |
| sceneEraseNvs(int idx) | WebSocket "clearScene" command | External command | Command |
| sceneRecall(int idx, uint16_t fadeMs, int outIdx) | Merge engine (LOSS_PRESET), sceneRecallHome, sceneTriggerPlay | Source loss or command | Internal |
| sceneRecallHome(int outIdx) | Merge engine (LOSS_HOME) | Source loss | Internal |

### Input (read-only)

- Scene table (per-output DMX frames, name, fadeTimeMs, triggerMask, priority, active flag) resident in PSRAM or DRAM, loaded from NVS at boot.
- Current DMX frame per output (read via the seqlock snapshot API).
- cfg.outputs[o].enabled (gates multi-output recursion during recall).
- millis() time source (drives fade elapsed-time computation on core 1).

### Output (written)

| Target | What | Core |
|---|---|---|
| DMX buffer seqlock | Interpolated frame bytes (channels 1-512) per output | Core 1 |
| Scene active flag | g_scenes[idx].active | Core 0 / Core 1 |
| NVS (scenes namespace) | Scene metadata, per-output frame chunks, home index | Core 0 (blocking) |

## 3. State Machine

Per-output fade state (2 states):

- **Idle** (active == false): No fade in progress. The output retains whatever frame was last committed to the seqlock buffer.
- **Fading** (active == true): sceneFadeStep is actively interpolating channels from "from" to "to". Transition to Idle occurs when elapsed milliseconds >= durationMs; the target frame is then committed in full.

Scene activation flag:

- **Inactive** (active == false): Scene is dormant; sceneCheckTimecodeTrigger skips it.
- **Active** (active == true): Scene is currently being recalled. Set true by sceneRecall; reset false when the scene is loaded from NVS and on subsequent idle transitions.

No other state machines exist in this module.

## 4. Data Flow

1. **Boot load**: The boot sequence invokes sceneLoadAll(), which allocates the scene array (PSRAM when available, internal heap as fallback) and loads all scene slots sequentially from NVS. For each slot, the 32-byte name blob and four 513-byte data chunks (one per output) are deserialized into the in-memory scene struct. The home scene index is loaded from the "home" key in the "dmxgw" namespace.

2. **Scene recall with fade**: sceneRecall(idx, fadeMs, outIdx) bounds-checks the scene index. When outIdx < 0, it recurses for every enabled output. For each output, the current DMX frame is snapshotted via the seqlock read API and used as the fade source; if the snapshot fails, the source is zero-filled. A per-output FadeState is initialized with the source frame, the scene's target frame, the fade duration, and the start timestamp. The scene's active flag is set true.

3. **Fade step (1 ms tick, core 1)**: The DMX frame tick calls sceneFadeStep(). For each output with an active fade, elapsed milliseconds are computed. If elapsed >= durationMs, the full target frame is committed to the seqlock buffer and the fade transitions to Idle. If durationMs > 0, each channel (1-512) is linearly interpolated as from[c] + (to[c] - from[c]) * (elapsed / durationMs) and written through the seqlock write API. The start-code byte (index 0) is never interpolated.

4. **Timecode trigger**: On receipt of an Art-Net TimeCode frame, sceneCheckTimecodeTrigger() scans every scene; any scene with triggerMask bit 0 set that is not already active is fired via sceneTriggerPlay(idx, fadeTimeMs).

5. **WebSocket scene trigger**: The WebSocket handler receives a scene playback command with an optional fade duration and calls sceneTriggerPlay(idx, fadeMs), which calls sceneRecall(idx, fadeMs, -1) to fade all enabled outputs.

6. **Source-loss recall**: When the merge engine detects source loss, it invokes sceneRecall(out.lossPreset, 0, outIdx) for LOSS_PRESET (instant recall) or sceneRecallHome(outIdx) for LOSS_HOME (also instant), committing the scene or home preset frame on the next fade step.

## 5. Configuration Integration

| Field | CFG flag | Default | How used |
|---|---|---|---|
| outputs[o].enabled | CFG_REBOOT | board template | Gates per-output recursion in sceneRecall; read live at call time |
| outputs[o].lossPreset | CFG_LIVE | board template | Selected by the merge engine on LOSS_PRESET, passed to sceneRecall |

The scene engine reads outputs[o].enabled live but writes no Config fields. Scene data is stored in NVS keys outside the CONFIG_FIELDS/OUTPUT_FIELDS schema table:

| NVS Key | Namespace | Type | Purpose |
|---|---|---|---|
| scn_s<idx>m | scenes | 32 bytes | Scene metadata (name) |
| scn_s<idx>c<chunk> | scenes | 513 bytes | Per-output DMX frame chunk |
| home | dmxgw | 1 byte | Home scene index |

## 6. Lifecycle

- **Init**: sceneLoadAll() is called from the boot sequence. Allocates the scene array, loads all scenes from NVS, and loads the home scene index.
- **Per-tick hook**: sceneFadeStep() is called from the 1 ms DMX frame tick on core 1 -- the only periodic invocation.
- **Event-driven**: sceneTriggerPlay (WebSocket/ArtTrigger/timecode), sceneSaveNvs/sceneEraseNvs (WebSocket save/clear), sceneRecall/sceneRecallHome (source-loss fallback via merge engine).
- **Shutdown**: None. Scene data persists in RAM for the device lifetime and in NVS across reboots.

## 7. Error Handling

| Function | Return | Failure condition |
|---|---|---|
| sceneSaveNvs | bool | idx out of range; scene array null; NVS open failure |
| sceneLoadNvs | bool | idx out of range; scene array null; NVS open failure; metadata key absent |
| sceneEraseNvs | bool | idx out of range |
| sceneTriggerPlay | bool | idx out of range |
| sceneRecall | void | Scene array null; invalid index silently ignored |
| sceneFadeStep | void | Invalid output index silently skipped |

NVS operations are FreeRTOS-safe but block the calling core until complete. No diagnostic logging is emitted by the scene engine itself. Invalid indices at the boundary between the engine and its callers are silently ignored.

## 8. Timing Constraints

| Constraint | Value | Core |
|---|---|---|
| Fade engine tick | 1 ms | Core 1 |
| Fade resolution | 1 ms | Core 1 |
| Instant recall | 0 ms | Core 1 (commits target on first tick) |
| NVS load (boot) | Sequential, blocking | Core 0 |
| Source-loss recall | Bounded by 1 ms tick | Core 1 |
| Interpolate channels | 512 per output | Core 1 (start-code byte excluded) |

The fade engine completes well within the 1 ms tick budget for the configured output count. NVS load occurs entirely on core 0 during boot and is blocking; scene-triggered playback after boot does not touch NVS.

## 9. Memory and Allocation Model

- **Scene array**: Allocated via heap_caps_malloc(MALLOC_CAP_SPIRAM) on PSRAM targets, with malloc fallback for non-PSRAM. Each Scene struct is ~2092 bytes (aligned). 32 scenes on PSRAM approximately 67 KB; 8 scenes on non-PSRAM approximately 17 KB.
- **Fade state**: static DRAM array, one FadeState per output (~1040 bytes each). 4 outputs approximately 4.2 KB static DRAM. Always DRAM-resident regardless of scene storage location.
- **Home scene index**: Single byte, static DRAM.
- **NVS I/O**: Preferences objects created on the stack per call -- no heap allocation.
- **No DMA or PSRAM allocation** within this module beyond scene storage.

## 10. Safety Considerations

- **Torn-read prevention**: All fade output is committed through the seqlock write API (dmxBufWriteBegin/dmxBufWriteEnd). The core-0 network writer never observes a partially written frame; it retries on odd seqlock tickets.
- **DMX continuity**: Scene fades write to the same seqlock buffer consumed by the transmit task. There is no window where the RMT peripheral reads an inconsistent frame. The DMX output runs uninterrupted between scene operations.
- **Source-loss fail-safe**: LOSS_HOLD (default) retains the last frame; LOSS_ZERO blanks to zero; LOSS_PRESET/LOSS_HOME arm a scene recall. None corrupt the transmit buffer.
- **Snapshot fallback**: If the seqlock snapshot fails during recall, the fade source is zero-filled, ensuring the interpolation always has valid bounds.
- **Output gating**: sceneRecall with outIdx < 0 recurses only over cfg.outputs[o].enabled, preventing writes to disabled or uninitialized outputs.
- **No heap on core 1**: Fade state is static DRAM; the 1 ms tick path never calls malloc, eliminating allocation-failure risk during real-time DMX output.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| core.dmx-buffer | read/write | Seqlock snapshot (read) on recall; seqlock write (commit) on fade step |
| core.merge-engine | calls sceneRecall() | Invokes scene recall on LOSS_PRESET / LOSS_HOME |
| sys.tasks | calls sceneFadeStep() | 1 ms DMX frame tick on core 1 |
| net.artnet | calls sceneCheckTimecodeTrigger() / sceneTriggerPlay() | Timecode trigger and ArtTrigger handling |
| net.websocket | calls sceneSaveNvs / sceneEraseNvs / sceneTriggerPlay | WebSocket scene, saveScene, clearScene commands |
| cfg (config_schema) | read | outputs[o].enabled for multi-output recursion |
| sys.boot | calls sceneLoadAll() | Boot-time scene load |
| drv.dmx-rmt-tx | indirect read | Consumes the seqlock buffer that fades write to |

## 12. Testing Verification

| Test Case | File | Validates |
|---|---|---|
| (none) | no scene test exists | The scene engine has no host-native test coverage |

**Untested**: sceneLoadAll, sceneSaveNvs, sceneEraseNvs, sceneTriggerPlay, sceneCheckTimecodeTrigger, sceneRecall, and the linear fade engine all have no unit-test coverage. Fade behavior is validated only on hardware via the soak-test monitor and serial console.

## 13. Open Questions

1. Whether sceneSave() (the full save including home index) is invoked from any code path outside the inspected callers, or whether the WebSocket handler's direct call to sceneSaveNvs() is the sole save entry point.
2. The measured NVS load latency for the full MAX_SCENES (32) library at boot, since sceneLoadAll is a blocking sequence before task creation.
3. Whether Scene.priority will be used for scene-vs-live-data takeover, or remains reserved.
4. Whether Phase 3 timecode matching will implement per-scene timecode matching (hh:mm:ss:frame) or retain the current "any timecode fires enabled scenes" behavior.

## 14. History

- **Initial design**: Scenes store full per-output DMX frames in PSRAM.
- **NVS chunking**: Scene data split into 4 chunks per scene (one per output) to work around the 512-byte NVS blob limit.
- **Fade engine co-located with DMX task**: Runs on core 1 alongside the 1 ms RMT transmit tick to ensure flicker-free interpolation without core-0 contention.
- **Timecode trigger**: Added as a placeholder; scenes with triggerMask bit 0 fire on any Art-Net TimeCode arrival; full per-scene matching deferred.
- **PSRAM-aware allocation**: Scene array allocated in PSRAM with internal-heap fallback for non-PSRAM targets.