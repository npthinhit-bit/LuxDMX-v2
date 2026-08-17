# Scene Engine — Technical Reference

Domain: core.scene-engine

## 1. Domain Scope

Owns scene-preset storage (NVS persistence + PSRAM RAM), the linear fade engine, and timecode-triggered scene playback for the LuxDMX-v2 firmware.

The module provides:

- Up to `MAX_SCENES` (32 on PSRAM targets, 8 on non-PSRAM) preset slots, each storing a full per-output DMX frame (`MAX_OUTPUTS` × `DMX_PACKET_SIZE`).
- NVS persistence with per-scene, per-output chunking (4 chunks × 513 bytes per scene) to work around the 512-byte NVS blob limit.
- A per-output linear fade engine that interpolates channel values from the current frame to the target frame over a caller-specified `fadeMs` duration.
- Timecode-triggered scene firing: scenes with `triggerMask` bit 0 set fire on any incoming Art-Net TimeCode frame.

Delegates nothing — the module does not parse network packets, merge senders, or drive the RMT peripheral directly. It reads the current DMX frame via the seqlock snapshot API and writes merged targets via the seqlock write API.

Consumers (callers):

- `src/sys/tasks.cpp:125` — `dmxFrameTick()` calls `sceneFadeStep()` every 1 ms tick on core 1.
- `src/main.cpp:109` — `setup()` calls `sceneLoadAll()` at boot.
- `src/core/merge_engine.cpp:46` — `mergeOutput()` calls `sceneRecall()` on `LOSS_PRESET` source loss.
- `src/core/merge_engine.cpp:49` — `mergeOutput()` calls `sceneRecallHome()` on `LOSS_HOME` source loss.
- `src/net/artnet.cpp:203` — `artHandlePacket()` calls `sceneCheckTimecodeTrigger()` on Art-Net TimeCode receive.
- `src/net/artnet.cpp:216` — `artHandlePacket()` calls `sceneTriggerPlay()` on ArtTrigger.
- `src/net/ws_handler.cpp:216` — WebSocket `"scene"` command handler calls `sceneTriggerPlay()`.
- `src/net/ws_handler.cpp:238` — WebSocket `"saveScene"` command calls `sceneSaveNvs()`.
- `src/net/ws_handler.cpp:246` — WebSocket `"clearScene"` command calls `sceneEraseNvs()`.
- `src/core/scene_engine.cpp:144` — `sceneRecall()` recurses for `outIdx < 0` (all enabled outputs).
- `src/core/scene_engine.cpp:164` — `sceneRecallHome()` calls `sceneRecall()`.
- `src/core/scene_engine.cpp:198` — `sceneTriggerPlay()` calls `sceneRecall()`.
- `src/core/scene_engine.cpp:212` — `sceneCheckTimecodeTrigger()` calls `sceneTriggerPlay()`.

## 2. Architecture Layer Mapping

```
drv  →  cfg  →  core  →  net  →  app/sys
                ↑         ↑      ↑
                |         |      |
                reads     calls  calls
                cfg       ws_    artnet
                (outputs) handler.cpp
```

The scene engine is a **core** layer module. It reads `cfg.outputs[o].enabled` from the **cfg** layer, writes DMX frame data via the **core** seqlock buffer (`dmx_buffer.h`), is called by the **sys** layer timing task (`tasks.cpp:125`), and is triggered by **net** layer consumers (WebSocket handler `ws_handler.cpp:216,238,246`, Art-Net opcode handler `artnet.cpp:203,216`).

## 3. Source Files

| File | Role |
|---|---|
| `src/core/scene_engine.h` | `Scene` struct (line 17), `MAX_SCENES` define (line 15), `g_scenes`/`g_sceneHome` externs (lines 26-27), all function declarations (lines 30-48) |
| `src/core/scene_engine.cpp` | `allocScenes()` (line 25), `FadeState` struct (line 40), `g_fade[]` (line 48), NVS I/O (`sceneSaveNvs` line 61, `sceneLoadNvs` line 77, `sceneEraseNvs` line 95), `sceneLoadAll` line 107, `sceneSave` line 125, `sceneRecall` line 136, `sceneRecallHome` line 163, `sceneFadeStep` line 168, `sceneTriggerPlay` line 196, `sceneCheckTimecodeTrigger` line 204 |
| `include/config_schema.h:9` | `MAX_OUTPUTS` constant (used in `Scene::data[MAX_OUTPUTS][...]` array sizing) |
| `include/config_schema.h:47` | `DmxOutput` struct (read for `.enabled` and `.port` fields) |
| `include/config_schema.h:91` | `extern Config cfg` — global config accessor |
| `include/rdm_types.h:33` | `DMX_PACKET_SIZE` (513) — frame size used in `Scene::data`, `FadeState::from/to` |
| `src/core/dmx_buffer.h:29` | `dmxBufferState()` accessor — `sceneFadeStep` calls `dmxBufferState().buffers[o].data[c]` |
| `src/core/dmx_buffer.h:32-33` | `dmxBufWriteBegin`/`dmxBufWriteEnd` — seqlock write bracketing used by `sceneFadeStep` |
| `src/core/dmx_buffer.h:44` | `dmxBufSnapshot` — called by `sceneRecall` to capture the current frame as fade source |
| `src/core/frame_router.h:8` | `portAddress()` — universe address computation (consumed indirectly via `cfg.outputs`) |
| `include/config_enums.h:7` | `DMX_IN_*` enums (not directly used by scene engine, but by input router which feeds into the scene pipeline) |

## 4. Data Structures

### `Scene` (`src/core/scene_engine.h:17-24`)

| Field | Type | Description |
|---|---|---|
| `name` | `char[32]` (line 18) | Human-readable preset name, NVS-persisted as 32-byte blob. |
| `fadeTimeMs` | `uint16_t` (line 19) | Default fade duration in milliseconds for this scene (0 = instant). |
| `triggerMask` | `uint8_t` (line 20) | Bitfield: bit 0 = timecode-trigger enabled. |
| `priority` | `uint8_t` (line 21) | Scene priority vs live network data (0-255). Reserved for future takeover logic. |
| `data` | `uint8_t[MAX_OUTPUTS][DMX_PACKET_SIZE]` (line 22) | Per-output DMX frame: start code at index 0, 512 slots at indices 1–512. |
| `active` | `bool` (line 23) | Whether this scene is currently being recalled (in-progress). Set `true` by `sceneRecall` at `scene_engine.cpp:160`; checked by `sceneCheckTimecodeTrigger` at `scene_engine.cpp:210` to prevent re-triggering. |

### `MAX_SCENES` (`src/core/scene_engine.h:8-15`)

`CONFIG_LUXDMX_MAX_SCENES` (default 32 when `CONFIG_SPIRAM_SUPPORT` defined, 8 otherwise), overridable via `-DCONFIG_LUXDMX_MAX_SCENES=N` build flag. `MAX_SCENES` resolves to this at `scene_engine.h:15`.

### `g_scenes` (`src/core/scene_engine.h:26` / `src/core/scene_engine.cpp:22`)

`extern Scene* g_scenes` — pointer to the PSRAM-allocated (or fallback DRAM) scene array. Starts `nullptr`; populated by `allocScenes()` (`scene_engine.cpp:25-37`).

### `g_sceneHome` (`src/core/scene_engine.h:27` / `src/core/scene_engine.cpp:23`)

`uint8_t g_sceneHome` — index of the "home" scene recalled on `LOSS_HOME`. Loaded from NVS key `"home"` in namespace `"dmxgw"` at `scene_engine.cpp:120`.

### `FadeState` (`src/core/scene_engine.cpp:40-47`)

| Field | Type | Description |
|---|---|---|
| `active` | `bool` (line 41) | Whether a fade is in progress for this output. |
| `startMs` | `uint32_t` (line 42) | `millis()` snapshot when the fade began. |
| `durationMs` | `uint32_t` (line 43) | Total fade duration in milliseconds. |
| `from` | `uint8_t[DMX_PACKET_SIZE]` (line 44) | Source frame (current frame at fade start). |
| `to` | `uint8_t[DMX_PACKET_SIZE]` (line 45) | Target frame (scene data for this output). |
| `outIdx` | `int` (line 46) | Output index this fade applies to. |

### `g_fade` (`src/core/scene_engine.cpp:48`)

`FadeState g_fade[MAX_OUTPUTS]` — one fade state per output, statically allocated in DRAM.

### NVS Keys (`src/core/scene_engine.cpp:51-59`)

| Key pattern | Type | Description |
|---|---|---|
| `"scn_s" + idx + "m"` | Namespace `scenes` | Scene metadata: 32-byte name blob. `scene_engine.cpp:57-59` |
| `"scn_s" + idx + "c" + chunk` | Namespace `scenes` | Per-output DMX frame chunk (513 bytes × 4 chunks for 4 outputs). `scene_engine.cpp:53-55` |
| `"home"` | Namespace `dmxgw` | Home scene index (1 byte). `scene_engine.cpp:120` |

## 5. Concurrency

**Dual-core, asymmetric.**

- **Core 1 (DMX task, priority 19):** `sceneFadeStep()` runs every 1 ms via `dmxFrameTick()` at `src/sys/tasks.cpp:125,141-142`. It reads `g_fade[]` (DRAM) and writes to `dmxBufferState().buffers[o].data[c]` via the seqlock write API (`dmxBufWriteBegin/WriteEnd`). The seqlock ensures the core-0 network writer does not observe torn fade output (`src/core/dmx_buffer.h:32-33`).
- **Core 0 (netRxTask, priority 5):** `sceneLoadAll()` (`main.cpp:109`), `sceneSaveNvs()` (`ws_handler.cpp:238`), `sceneEraseNvs()` (`ws_handler.cpp:246`), `sceneTriggerPlay()` (`ws_handler.cpp:216`), and `sceneCheckTimecodeTrigger()` (`artnet.cpp:203`) all execute on core 0 via their respective callers. NVS I/O is FreeRTOS-safe but blocks the caller until complete.
- **No mutex** protects `g_scenes` or `g_fade`. The comment at `scene_engine.cpp:5-6` states "The fade engine runs on core 1 (DMX task)." Only `sceneFadeStep()` (core 1) writes `g_fade[]`; only `sceneRecall()` (called from any core) writes `g_scenes[].active` and `g_fade[]` initialization. `sceneRecall` is called from core 0 (merge_engine:46, ws_handler:216) and core 1 (timecode trigger is core 0; LOSS_PRESET via mergeOutput is core 1 at `tasks.cpp:135`).

## 6. State Machine

Per-output fade state machine (2 states, per-output):

- **Idle** (`g_fade[o].active == false`): no fade in progress. `scene_engine.cpp:172` skips inactive outputs.
- **Fading** (`g_fade[o].active == true`): interpolating from `from[]` to `to[]` over `durationMs`. Transition to Idle occurs when `elapsed >= durationMs` at `scene_engine.cpp:174` — the `to[]` frame is committed and `active` is set false at `scene_engine.cpp:179`.

Scene activation flag (`Scene::active` in `g_scenes[]`):

- **Inactive** (`active == false`): scene is dormant. `scene_engine.cpp:210` skips already-active scenes in timecode trigger.
- **Active** (`active == true`): scene is currently recalled. Set by `sceneRecall()` at `scene_engine.cpp:160`; set false on load from NVS at `scene_engine.cpp:90,113`.

No other state machine exists in this module.

## 7. Entry Points

| Entry point | Address | Caller |
|---|---|---|
| `sceneLoadAll()` | `src/core/scene_engine.cpp:107` | `setup()` at `src/main.cpp:109` |
| `sceneFadeStep()` | `src/core/scene_engine.cpp:168` | `dmxFrameTick()` at `src/sys/tasks.cpp:125` |
| `sceneRecall(int, uint16_t, int)` | `src/core/scene_engine.cpp:136` | `merge_engine.cpp:46` (LOSS_PRESET), `scene_engine.cpp:164` (via `sceneRecallHome`), `scene_engine.cpp:198` (via `sceneTriggerPlay`) |
| `sceneRecallHome(int)` | `src/core/scene_engine.cpp:163` | `merge_engine.cpp:49` (LOSS_HOME) |
| `sceneTriggerPlay(int, uint16_t)` | `src/core/scene_engine.cpp:196` | `artnet.cpp:216` (ArtTrigger), `ws_handler.cpp:216` (WebSocket `"scene"` command), `scene_engine.cpp:212` (timecode trigger) |
| `sceneCheckTimecodeTrigger()` | `src/core/scene_engine.cpp:204` | `artnet.cpp:203` (ArtTimeCode receive) |
| `sceneSaveNvs(int)` | `src/core/scene_engine.cpp:61` | `ws_handler.cpp:238` (WebSocket `"saveScene"`) |
| `sceneEraseNvs(int)` | `src/core/scene_engine.cpp:95` | `ws_handler.cpp:246` (WebSocket `"clearScene"`) |
| `sceneSave(int)` | `src/core/scene_engine.cpp:125` | Caller not identified in inspected source — see [Open Questions](#18-open-questions). |

## 8. Data Flow

1. **Boot load** (core 0): `setup()` → `sceneLoadAll()` (`main.cpp:109`) → `allocScenes()` allocates the `Scene` array in PSRAM or fallback DRAM (`scene_engine.cpp:108,25-37`) → loop over `MAX_SCENES` calling `sceneLoadNvs(i)` (`scene_engine.cpp:111-115`) → if a scene key exists in NVS namespace `"scenes"`, name + 4 data chunks are loaded into `g_scenes[i]` (`scene_engine.cpp:84-89`) and `active` is set false (`scene_engine.cpp:90`) → home scene index loaded from `"home"` key in `"dmxgw"` namespace (`scene_engine.cpp:118-122`).
2. **Scene recall with fade** (core 1 or core 0): `sceneRecall(presetIdx, fadeMs, outIdx)` is called (`scene_engine.cpp:136`) → bounds-checked (`scene_engine.cpp:138`) → if `outIdx < 0`, recurses for every enabled output (`scene_engine.cpp:140-146`) → current frame snapshotted via `dmxBufSnapshot(outIdx, cur)` as the fade source (`scene_engine.cpp:150`) — if the snapshot fails, `cur` is zeroed (`scene_engine.cpp:151`) → `FadeState` populated: `active=true`, `startMs=millis()`, `durationMs=fadeMs`, `from=cur`, `to=scene.data[outIdx]`, `outIdx=outIdx` (`scene_engine.cpp:153-159`) → `g_scenes[presetIdx].active = true` (`scene_engine.cpp:160`).
3. **Fade step** (core 1, 1 ms tick): `dmxTxTask` → `dmxFrameTick()` → `sceneFadeStep()` (`tasks.cpp:125,168`) → for each output, if `g_fade[o].active`, compute `elapsed = millis() - startMs` (`scene_engine.cpp:173`) → if `elapsed >= durationMs` (fade complete): write `gs_fade[o].to` data into `dmxBufferState().buffers[o].data[1..512]` via seqlock write, set `active=false` (`scene_engine.cpp:176-179`) → else if `durationMs > 0`: linearly interpolate each channel `from[c] + (to[c]-from[c]) * (elapsed/durationMs)` and write via seqlock (`scene_engine.cpp:180-189`).
4. **Timecode trigger** (core 0): `artHandlePacket()` receives Art-Net TimeCode → calls `sceneCheckTimecodeTrigger()` (`artnet.cpp:203`) → for each scene with `triggerMask & 0x01` and `!active`, calls `sceneTriggerPlay(i, fadeTimeMs)` (`scene_engine.cpp:209-212`).
5. **WebSocket scene trigger** (core 0): `wsHandler` receives `{"cmd":"scene","play":n,"fade":ms}` → calls `sceneTriggerPlay(sceneIdx, fadeMs)` (`ws_handler.cpp:216`) → calls `sceneRecall(idx, fadeMs, -1)` (`scene_engine.cpp:198`) → fade proceeds on next `dmxFrameTick`.
6. **Source-loss recall** (core 1): `mergeOutput()` detects source loss → `sceneRecall(out.lossPreset, 0, outIdx)` on `LOSS_PRESET` (`merge_engine.cpp:46`) or `sceneRecallHome(outIdx)` on `LOSS_HOME` (`merge_engine.cpp:49`) → instant recall (fadeMs=0) writes target frame immediately on next `sceneFadeStep`.

## 9. Protocol Layout

N/A (no wire protocol). The scene engine consumes and produces decoded DMX frame payloads (513 bytes per output: 1 start code byte + 512 slot bytes, as defined by `DMX_PACKET_SIZE` at `include/rdm_types.h:33`). Wire protocol formats for incoming data originate from Art-Net (`[net-artnet-protocol](./net-artnet-protocol.md)`) and sACN (`[net-sacn-protocol](./net-sacn-protocol.md)`). WebSocket text commands are documented in `[net-websocket-handler](./net-websocket-handler.md)`.

## 10. Config Integration

Reads:
- `cfg.outputs[o].enabled` (`include/config_schema.h:12`) — gates recursion in `sceneRecall()` at `scene_engine.cpp:143`. This is a `CFG_REBOOT` field (`include/config_enums.h` — pin/hardware bound), but the scene engine reads it live at call time; enabling/disabling an output takes effect on the next `sceneRecall` call.

Write: none. The scene engine does not write any `Config` fields back.

NVS namespace: scene data is stored in Namespace `"scenes"` (`scene_engine.cpp:51`), home index in `"dmxgw"` (`scene_engine.cpp:119`). These are not part of the `CONFIG_FIELDS[]`/`OUTPUT_FIELDS[]` schema tables — they are scene-module-specific NVS keys.

## 11. Lifecycle

- **Init**: `sceneLoadAll()` called from `setup()` at `src/main.cpp:109`. Allocates scene array via `allocScenes()`, loads all scenes from NVS, loads home scene index.
- **Runtime — per-tick**: `sceneFadeStep()` called from `dmxFrameTick()` on core 1 every 1 ms (`tasks.cpp:125`). This is the only periodic hook.
- **Runtime — event-driven**: `sceneTriggerPlay()` (WebSocket/ArtTrigger), `sceneCheckTimecodeTrigger()` (ArtTimeCode), `sceneRecall()` (source-loss fallback in merge engine), `sceneSaveNvs()`/`sceneEraseNvs()` (WebSocket save/clear), `sceneLoadAll()` (boot only).
- **Shutdown**: None. Scene data persists in PSRAM/DRAM for the device lifetime; NVS entries persist across reboots.

## 12. Error Handling

- `sceneSaveNvs(int idx)` returns `bool`: `false` if `idx` out of `[0, MAX_SCENES)` range (`scene_engine.cpp:62`) or if `g_scenes` is null (`scene_engine.cpp:63`) or if NVS `begin()` fails (`scene_engine.cpp:65`). Returns `true` on success (`scene_engine.cpp:74`).
- `sceneLoadNvs(int idx)` returns `bool`: `false` if `idx` out of range (`scene_engine.cpp:78`) or `g_scenes` null (`scene_engine.cpp:79`) or NVS `begin` fails (`scene_engine.cpp:81`) or the meta key does not exist (`scene_engine.cpp:82`). Returns `true` on successful load (`scene_engine.cpp:92`).
- `sceneEraseNvs(int idx)` returns `bool`: `false` if `idx` out of range (`scene_engine.cpp:96`). Returns `true` on success (`scene_engine.cpp:104`).
- `sceneRecall()`, `sceneRecallHome()`, `sceneFadeStep()`, `sceneCheckTimecodeTrigger()` return `void` — invalid indices are silently ignored (`scene_engine.cpp:150` checks `g_scenes` null; `scene_engine.cpp:160` bounds-check via array index safety at call sites).
- `sceneTriggerPlay(int idx, uint16_t fadeMs)` returns `bool`: `false` if `idx` out of range (`scene_engine.cpp:197`), `true` on success (`scene_engine.cpp:199`).
- No `ESP_LOGE`/`Serial.printf` error logging from the scene engine itself (the comment at `scene_engine.cpp:207-208` notes Phase 3 placeholder behavior for timecode triggers).

## 13. Memory Allocation

- `g_scenes` (Scene array): allocated via `heap_caps_malloc(sizeof(Scene) * MAX_SCENES, MALLOC_CAP_SPIRAM)` on PSRAM targets (`scene_engine.cpp:28`), with fallback to `malloc()` if PSRAM allocation fails (`scene_engine.cpp:31`) or if `CONFIG_SPIRAM_SUPPORT` is not defined (`scene_engine.cpp:34`). Zeroed via `memset` at `scene_engine.cpp:36`.
  - `sizeof(Scene)` = 32 (name) + 2 (fadeTimeMs) + 1 (triggerMask) + 1 (priority) + 2052 (data[4][513]) + 1 (active) + padding = 2089 bytes (aligned to 4-byte boundary: 2092 bytes).
  - `MAX_SCENES = 32` (PSRAM) → 32 × 2092 ≈ 66.9 KB in PSRAM.
  - `MAX_SCENES = 8` (non-PSRAM) → 8 × 2092 ≈ 16.7 KB in internal heap.
- `g_fade` (FadeState array): **static DRAM** at `scene_engine.cpp:48`. `sizeof(FadeState)` = 1 (active) + 4 (startMs) + 4 (durationMs) + 513 (from) + 513 (to) + 4 (outIdx) + padding = 1039 bytes aligned to 1040. × `MAX_OUTPUTS` (4) = ~4.2 KB static DRAM.
- `g_sceneHome`: **static DRAM**, single byte at `scene_engine.cpp:23`.
- NVS I/O uses `Preferences` objects created on the stack per call (`scene_engine.cpp:64,80,97,109,128`) — no heap allocation for NVS.
- No `MALLOC_CAP_DMA` or `MALLOC_CAP_INTERNAL` allocation — scenes live in PSRAM (when available) or internal heap (fallback). Fade state is always DRAM-static.

## 14. Timing

- **Fade engine tick**: 1 ms period, driven by `dmxTxTask` via `vTaskDelayUntil(&xLastWakeTime, 1)` at `src/sys/tasks.cpp:142`. `sceneFadeStep()` is called within `dmxFrameTick()` at `src/sys/tasks.cpp:125`.
- **Fade resolution**: linear interpolation per channel per 1 ms tick (`scene_engine.cpp:182-188`). A fade with `durationMs=0` is instant — `sceneRecall` sets `durationMs=0`, and `sceneFadeStep` checks `elapsed >= durationMs` immediately (`scene_engine.cpp:174`), committing the target frame on the first tick.
- **NVS load (boot)**: all `MAX_SCENES` scenes loaded sequentially from NVS during `sceneLoadAll()` (`scene_engine.cpp:107-123`). No measured latency is recorded in source — this is a blocking operation on core 0 before tasks are created.
- **Input router poll**: `inputRouterPoll()` is called from `loop()` on core 0 (`main.cpp:153`) — not on the 1 ms DMX tick, so DMX-in→scene-trigger latency is bounded by `loop()` iteration time.

## 15. Traceability

| Claim | Evidence |
|---|---|
| `MAX_SCENES` is 32 on PSRAM, 8 otherwise | `src/core/scene_engine.h:8-15` |
| `Scene` struct has `name`, `fadeTimeMs`, `triggerMask`, `priority`, `data[MAX_OUTPUTS][513]`, `active` | `src/core/scene_engine.h:17-24` |
| `Scene::active` is set true on recall, false on NVS load | `src/core/scene_engine.cpp:90,113,160` |
| `g_scenes` is nullptr until `allocScenes()` | `src/core/scene_engine.h:26`; `scene_engine.cpp:22,26` |
| PSRAM allocation with `MALLOC_CAP_SPIRAM` and `malloc` fallback | `src/core/scene_engine.cpp:27-35` |
| Array is zero-initialized after alloc | `src/core/scene_engine.cpp:36` |
| `FadeState` has active/startMs/durationMs/from/to/outIdx | `src/core/scene_engine.cpp:40-47` |
| `g_fade` is one FadeState per output, static DRAM | `src/core/scene_engine.cpp:48` |
| NVS namespace is `"scenes"` | `src/core/scene_engine.cpp:51` |
| Scene name key: `"scn_s" + idx + "m"`, 32 bytes | `src/core/scene_engine.cpp:57-59,69` |
| Scene data key: `"scn_s" + idx + "c" + chunk`, 513 bytes | `src/core/scene_engine.cpp:53-55,71` |
| Home scene index stored under `"home"` in `"dmxgw"` namespace | `src/core/scene_engine.cpp:119-121` |
| `sceneSaveNvs` bounds-checks idx and g_scenes | `src/core/scene_engine.cpp:62-63` |
| `sceneSaveNvs` writes 32-byte name + 4×513-byte chunks | `src/core/scene_engine.cpp:69-72` |
| `sceneLoadNvs` checks key existence before reading | `src/core/scene_engine.cpp:82` |
| `sceneLoadNvs` null-terminates name at byte 31 | `src/core/scene_engine.cpp:86` |
| `sceneLoadNvs` sets `active = false` on load | `src/core/scene_engine.cpp:90` |
| `sceneLoadAll` calls `allocScenes()` first | `src/core/scene_engine.cpp:108` |
| `sceneLoadAll` loads home index from NVS | `src/core/scene_engine.cpp:118-122` |
| `sceneRecall` snapshots current frame via `dmxBufSnapshot` | `src/core/scene_engine.cpp:150` |
| `sceneRecall` zero-fills on snapshot failure | `src/core/scene_engine.cpp:151` |
| `sceneRecall` recurses for all enabled outputs when `outIdx < 0` | `src/core/scene_engine.cpp:140-146` |
| `sceneRecall` checks `cfg.outputs[o].enabled` | `src/core/scene_engine.cpp:143` |
| Fade completion writes `to` data via seqlock | `src/core/scene_engine.cpp:176-178` |
| Fade interpolation uses float `frac = elapsed / durationMs` | `src/core/scene_engine.cpp:182` |
| `sceneFadeStep` interpolates channels 1 through 512 (skips start code at 0) | `src/core/scene_engine.cpp:184` |
| `sceneRecallHome` calls `sceneRecall` with fadeMs=0 | `src/core/scene_engine.cpp:164` |
| `sceneTriggerPlay` bounds-checks idx | `src/core/scene_engine.cpp:197` |
| Timecode trigger checks `triggerMask & 0x01` | `src/core/scene_engine.cpp:211` |
| Timecode trigger skips already-active scenes | `src/core/scene_engine.cpp:210` |
| `sceneFadeStep` is called from `dmxFrameTick` on core 1 | `src/sys/tasks.cpp:125` |
| `sceneLoadAll` is called from `setup()` | `src/main.cpp:109` |
| `sceneRecall` is called on `LOSS_PRESET` (merge_engine.cpp:46) and `LOSS_HOME` (merge_engine.cpp:49) | `src/core/merge_engine.cpp:46,49` |
| `sceneTriggerPlay` is called from WebSocket handler | `src/net/ws_handler.cpp:216` |
| `sceneTriggerPlay` is called from ArtTrigger opcode handler | `src/net/artnet.cpp:216` |
| `sceneCheckTimecodeTrigger` is called from ArtTimeCode handler | `src/net/artnet.cpp:203` |
| `sceneSaveNvs` is called from WebSocket `"saveScene"` handler | `src/net/ws_handler.cpp:238` |
| `sceneEraseNvs` is called from WebSocket `"clearScene"` handler | `src/net/ws_handler.cpp:246` |
| `DMX_PACKET_SIZE` is 513 | `include/rdm_types.h:33` |
| `MAX_OUTPUTS` is 4 (default) | `include/config_schema.h:7-9` |
| Comment: "The fade engine runs on core 1 (DMX task)" | `src/core/scene_engine.cpp:5-6` |

## 16. Cross-References

- `./core-dmx-buffer.md` — `dmxBufSnapshot()` used by `sceneRecall` to capture the current frame (`scene_engine.cpp:150`); `dmxBufWriteBegin/WriteEnd` and `dmxBufferState()` used by `sceneFadeStep` to commit interpolated frames (`scene_engine.cpp:176-189`).
- `./core-merge-engine.md` — calls `sceneRecall()` on `LOSS_PRESET` (`merge_engine.cpp:46`) and `sceneRecallHome()` on `LOSS_HOME` (`merge_engine.cpp:49`).
- `./core-frame-router.md` — not a direct caller, but `routeFrame` feeds frames that scenes may override.
- `./core-sender-tracker.md` — not directly used by the scene engine, but `updateSender`/`sourceStatus` are part of the same input→merge→output pipeline.
- `./sys-tasks.md` — `dmxFrameTick()` calls `sceneFadeStep()` every 1 ms on core 1 (`src/sys/tasks.cpp:125`).
- `./drv-dmx-uart-rx.md` — documents `DmxInFrame` and `dmxInPoll()` which feed the DMX-in retransmit path (`input_router.cpp:21`) that ultimately enters the scene pipeline via `routeFrame`.
- `./net-artnet-protocol.md` — references timecode trigger (`sceneCheckTimecodeTrigger` at `artnet.cpp:203`) and ArtTrigger scene playback (`sceneTriggerPlay` at `artnet.cpp:216`).
- `./net-websocket-handler.md` — WebSocket `"scene"`, `"saveScene"`, `"clearScene"` commands call `sceneTriggerPlay`, `sceneSaveNvs`, `sceneEraseNvs` (`ws_handler.cpp:216,238,246`).
- `./config-engine.md` — `cfg.outputs[o].enabled` is a `CFG_REBOOT` field resolved via the config engine (`main.cpp:109,143`).
- `./include-headers.md` — documents `DMX_PACKET_SIZE` (`include/rdm_types.h:33`) and `MAX_OUTPUTS` (`include/config_schema.h:9`).

## 17. Limitations

- `sceneSave()` (`scene_engine.cpp:125`) has no identified caller in the inspected source — `ws_handler.cpp:238` calls `sceneSaveNvs()` directly, bypassing `sceneSave()`. The full `sceneSave` (which also persists the home index) may be called from code outside the inspected files.
- NVS blob limit of 512 bytes forces scene data to be split into 4 chunks per scene (`scene_engine.cpp:1,3-4`) — one chunk per output × `DMX_PACKET_SIZE` (513 bytes). This is a workaround, not an inherent constraint of `Scene` storage.
- `Scene::priority` (`scene_engine.h:21`) is declared but never read in the inspected source — it is reserved for future scene-vs-live-source priority takeover.
- `Scene::triggerMask` only checks bit 0 (`scene_engine.cpp:211`) — other trigger bits are not implemented (Phase 3 placeholder at `scene_engine.cpp:207-208`).
- `sceneRecall` with `outIdx < 0` recurses synchronously for all enabled outputs (`scene_engine.cpp:142-146`) — if many outputs are enabled, this blocks the calling task (core 0 WebSocket handler or core 1 merge path) while snapshotting each output.
- Fade interpolation uses `float` arithmetic (`scene_engine.cpp:182`) — on ESP32-S3 without FPU optimizations, this may add per-tick jitter for output-heavy configurations.

## 18. Open Questions

1. Not determinable from the inspected source code — whether `sceneSave()` (`scene_engine.cpp:125`) is called from any code path outside the inspected files, or whether the WebSocket handler's direct call to `sceneSaveNvs()` (`ws_handler.cpp:238`) is the only save entry point.
2. Not determinable from the inspected source code — whether Phase 3 timecode matching (`scene_engine.cpp:207-208`) will implement per-scene timecode matching (hh:mm:ss:frame) or replace the current "any timecode → fire" behavior.
3. Not determinable from the inspected source code — the measured NVS load latency for `MAX_SCENES` (32) scenes at boot, since `sceneLoadAll()` is a blocking sequence (`scene_engine.cpp:107-123`) on core 0 before `createTasks()` runs.
4. Not determinable from the inspected source code — whether `Scene::priority` will be used for scene-vs-live-data takeover, or whether it remains purely reserved.

## 19. Testing

No dedicated unit test or native test exists for the scene engine. The inspectable test files are:

- `test/native/merge_test.cpp` — tests `merge_engine.cpp` which calls `sceneRecall` (`merge_engine.cpp:46,49`), but the test stubs at `src/test_stubs.cpp:12` replace `sceneRecall` and `sceneRecallHome` with no-ops — the scene engine itself is not exercised.
- `test/native/config_test.cpp`, `test/native/seqlock_test.cpp`, `test/native/rdm_types_test.cpp` — no scene engine coverage.
- No `test/native/scene_test.cpp` or `test/unit-test/test_scene_engine.cpp` exists in the inspected tree.
- The fade engine is validated only on hardware via the soak-test monitor workflow (`src/sys/soak_monitor.cpp`) and the serial console (`config_serial.cpp`).

## 20. History

- Initial design: scenes stored as full per-output DMX frames in PSRAM (`src/core/scene_engine.cpp:1-6` comment: "Stores up to MAX_SCENES presets, each with a full 4-universe DMX frame").
- NVS chunking strategy: each scene stored as 4 chunks (one per output) to work around the 512-byte NVS blob limit (`scene_engine.cpp:1-4` comment).
- Fade engine co-designed with `dmxTxTask`: runs on core 1 alongside the RMT transmit tick (`scene_engine.cpp:5-6` comment: "The fade engine runs on core 1 (DMX task)").
- Timecode trigger added as a Phase 3 placeholder (`scene_engine.cpp:207-208`): scenes with `triggerMask` bit 0 fire on any Art-Net TimeCode arrival, with full per-scene timecode matching deferred.
- PSRAM-aware allocation: `allocScenes()` uses `heap_caps_malloc(MALLOC_CAP_SPIRAM)` with `malloc` fallback for non-PSRAM targets (`scene_engine.cpp:17-35`).
