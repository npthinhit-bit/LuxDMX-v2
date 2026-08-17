# Output Init — Technical Reference

Domain: core.output-init

## 1. Domain Scope

Owns the runtime initialization of DMX output channels: RMT TX channel allocation, optional UART RX for RDM, DE/RE GPIO configuration, RDM line mapping, and the crash-guard-wrapped init sequence. The module bridges the **drv** layer (RMT, UART) and the **core**/**sys** layers (DMX task, RDM task) by populating the `g_outputs[]` runtime table that the transmit task owns.

Key responsibilities:
- `sanitizeOutputs()`: disables any output whose `txPin` is invalid (< 0).
- `outputInitAll()`: iterates all enabled outputs, initializes RMT channels, optionally initializes DMX input (converter mode) and RDM lines, and builds the `rdmLineForOut`/`rdmOutForLine` bidirectional mapping.
- `viewOutput()`: returns the output index used for serial/WebSocket "monitor" display (first enabled, or `monitorOut`).
- `rdmOutSelect()`: maps an output index to its RDM line and calls `rdmRmtSelect()`.
- `updateOutputRuntime()`: applies live config changes to break/MAB/invert on an already-initialized output.
- `dmxIsDelta()`: checks whether an output is in delta (one-frame-per-packet) TX style.

Delegates RMT hardware init to `[drv-dmx-rmt-tx](./drv-dmx-rmt-tx.md)` (`rmtDmxInit`), DMX input init to `[drv-dmx-uart-rx](./drv-dmx-uart-rx.md)` (`dmxInInit`), and RDM line selection to `[core-rdm-engine](./core-rdm-engine.md)` (`rdmRmtInit`/`rdmRmtSelect`).

Consumers:
- `src/main.cpp:47` — calls `sanitizeOutputs()` after `cfgcore::load()`.
- `src/main.cpp:106-108` — calls `dmxInitGuardBegin()` → `outputInitAll()` → `dmxInitGuardEnd()` in the crash-safe init sequence.
- `src/sys/tasks.cpp:97,101,128` — `outReady[i]`, `g_outputs[i].rmt`, `cfg.outputs[i].enabled` checked per tick in `snapshotAndTransmit` and `dmxFrameTick`.
- `[core-frame-router](./core-frame-router.md):24` — `viewOutput()` selects the monitor output for logging.
- `[core-rdm-engine](./core-rdm-engine.md)` / `[core-rdm-task](./core-rdm-task.md)` — `rdmOutSelect()` selects the active RDM line (`src/core/output_init.cpp:32`).

## 2. Layer Mapping

```
drv  →  cfg  →  core  →  net  →  app/sys
 ↑              ↑               ↑
 |              |               |
 rmtDmxInit      |             rdmOutSelect() →
 (dmx_rmt.h)     |             rdmRmtSelect()   [core-rdm-engine]
 dmxInInit       |             |
 (dmx_input.h)   |             |
 rdmRmtInit      |             |
 (rdm_engine.h)  |             |
                 |             ↓
               reads cfg    g_outputs[]
                (per-output (populated
                 pin fields)  here)
```

The output init module spans the **core** layer but calls **drv** layer drivers (`dmx_rmt.h`, `dmx_input.h`) to configure hardware. It is invoked from **sys** layer setup (`main.cpp:106-108`) and its outputs are consumed by the **sys** layer `dmxTxTask` (`tasks.cpp:83,96-108`).

## 3. Source Files

| File | Role |
|---|---|
| `src/core/output_init.cpp` | `sanitizeOutputs()` (line 8), `outputInitAll()` (line 35), `viewOutput()` (line 22), `rdmOutSelect()` (line 29), `dmxIsDelta()` (line 113), `updateOutputRuntime()` (line 117), extern globals (lines 14-20) |
| `src/core/output_init.h` | Declarations for all functions + extern globals (`g_outputs[]`, `outReady[]`, `dmxReady`, `monitorOut`, `rdmOut`, `rdmLineForOut[]`, `rdmOutForLine[]`) |
| `include/output.h` | `output_mode_t` enum (line 11), `dmx_output_t` struct (line 16), `resolveOutputMode()` inline (line 28) |
| `src/drv/dmx_rmt.h:109` | `rmtDmxInit()` and `RmtDmx` struct — called by `outputInitAll` |
| `src/drv/dmx_input.h:24` | `dmxInInit()` — called when `inputMode != DMX_IN_OFF` and `rxPin >= 0` |
| `src/core/rdm_engine.h:57,60` | `rdmRmtSelect()` and `rdmRmtInit()` — called for RDM-capable outputs |
| `src/sys/tasks.cpp:43-76` | `dmxInitGuardBegin()` / `dmxInitGuardEnd()` — crash guard wrappers around `outputInitAll` |
| `include/config_enums.h:11-13` | `DMX_IN_OFF`, `DMX_IN_TO_NET`, `DMX_IN_MONITOR` |
| `include/config_enums.h:24-26` | `TXSTYLE_CONTINUOUS`, `TXSTYLE_DELTA`, `TXSRC_LOCAL`, `TXSRC_ARTNET` |

## 4. Data Structures

### `output_mode_t` (`include/output.h:11-14`)

| Value | Constant | Description |
|---|---|---|
| 0 | `OUTPUT_MODE_DMX_ONLY` | RMT TX only, auto-direction RS485, no RDM. |
| 1 | `OUTPUT_MODE_RDM_FULL` | RMT TX + UART RX + DE/RE GPIO, RDM E1.20 capable. |

### `dmx_output_t` (`include/output.h:16-26`)

| Field | Type | Description |
|---|---|---|
| `rmt` | `RmtDmx` (line 17) | RMT TX channel + symbol buffer (from `src/drv/dmx_rmt.h:31`). |
| `index` | `int` (line 18) | Output index (0-based). |
| `rmtChannel` | `int` (line 19) | RMT channel number assigned during init. |
| `mode` | `output_mode_t` (line 20) | DMX-only or RDM-full. |
| `dePin` | `int` (line 21) | DE/RE GPIO pin for half-duplex direction control. |
| `rxPin` | `int` (line 22) | UART RX pin (for RDM response / DMX input). |
| `uartPort` | `uart_port_t` (line 23) | UART peripheral for RX (UART1 or UART2). `UART_NUM_MAX` if unused. |
| `ready` | `bool` (line 24) | True after successful RMT init. |
| `seq` | `volatile uint32_t` (line 25) | Seqlock-style version field for this output's runtime state. |

### `resolveOutputMode` (`include/output.h:28-32`)

```c
inline output_mode_t resolveOutputMode(int modeVal, int rtsPin)
```

Returns `OUTPUT_MODE_RDM_FULL` if `modeVal == OUTPUT_MODE_RDM_FULL` (line 29) or if `rtsPin >= 0` (line 30). Otherwise returns `OUTPUT_MODE_DMX_ONLY` (line 31). Called from `outputInitAll` at `src/core/output_init.cpp:64`.

### Runtime globals (`src/core/output_init.cpp:14-20`)

| Name | Type | Initial | Description |
|---|---|---|---|
| `g_outputs` | `dmx_output_t[MAX_OUTPUTS]` | `{}` | Populated by `outputInitAll()`. |
| `outReady` | `bool[MAX_OUTPUTS]` | `{false}` | True when RMT init succeeded. |
| `dmxReady` | `bool` | `false` | True when ≥1 output is ready. |
| `monitorOut` | `int` | `0` | Default monitor/view output. |
| `rdmOut` | `int` | `-1` | First RDM-capable output, or -1. |
| `rdmLineForOut` | `int[MAX_OUTPUTS]` | `{-1,-1,-1,-1}` | Output → RDM line index. |
| `rdmOutForLine` | `int[MAX_OUTPUTS]` | `{-1,-1,-1,-1}` | RDM line → output index. |

## 5. Concurrency

**Single-threaded (core 0, `setup()`). Runtime reads are lock-free.**

- `outputInitAll()` runs during `setup()` on core 0, before `createTasks()` spawns `dmxTxTask` on core 1 (`src/main.cpp:107,130`). No concurrency during init.
- `g_outputs[]`, `outReady[]`, `monitorOut`, `rdmOut`, `rdmLineForOut[]`, `rdmOutForLine[]` are written once during `outputInitAll()` and then **read-only** by `dmxTxTask` on core 1 (`src/sys/tasks.cpp:97,101,128`).
- The `seq` field in `dmx_output_t` (`include/output.h:25`) is `volatile` — it is intended as a lightweight version counter that the core-1 reader can check, but the inspected source does not use it for any synchronization (see [Open Questions](#18-open-questions)).
- `updateOutputRuntime()` (`src/core/output_init.cpp:117-124`) writes `breakTime`/`mabTime`/`invert` on `g_outputs[outIdx].rmt` at runtime — called from the web config apply path (caller not in inspected source; see [Open Questions](#18-open-questions)). This writes to a struct that core 1 reads via `rmtDmxIdle`/`rmtDmxKick` (`src/drv/dmx_rmt.h:172-192`). The RMT driver's own internal lock handles concurrent access to `rmt_transmit`, but the `breakTime`/`mabTime`/`invert` field writes are not explicitly synchronized against the core-1 reader.
- `dmxReady`, `dmxInitGuardBegin/End` are in `src/sys/tasks.cpp` — the crash guard counter is stored in NVS under the `"dmxgw"` namespace (`src/sys/tasks.cpp:40`).

## 6. State Machine

The crash guard (`dmxInitGuardBegin`/`dmxInitGuardEnd`) implements a small NVS-backed state machine:

- **Reset (counter == 0)**: `dmxInitGuardBegin` reads `s_crashCount = 0`; no outputs are disabled; `outputInitAll` proceeds normally (`src/sys/tasks.cpp:43-55`).
- **Crash recovery (counter > 0)**: `dmxInitGuardBegin` disables outputs from index `s_crashCount - 1` down to 0 (`src/sys/tasks.cpp:48-54`). This is progressive: each crash disables one more output (the highest-indexed remaining).
- **Stable boot (guard end)**: `dmxInitGuardEnd` writes `s_crashCount + 1` to NVS, then waits 3000 ms (`DMX_GUARD_TTL_MS` at `src/sys/tasks.cpp:41,62-65`). If the device survives without panicking, the counter is reset to 0 (`src/sys/tasks.cpp:66-75`). If it panics, the counter persists and the next boot disables one more output.

`outputInitAll` itself is stateless — it iterates outputs once and sets runtime fields.

## 7. Entry Points

| Entry point | Address | Caller |
|---|---|---|
| `sanitizeOutputs()` | `src/core/output_init.cpp:8` | `setup()` — first init phase (`src/main.cpp:47`) |
| `outputInitAll()` | `src/core/output_init.cpp:35` | `setup()` inside crash guard (`src/main.cpp:106-108`) |
| `viewOutput()` | `src/core/output_init.cpp:22` | `routeFrameImpl()` monitor log (`src/core/frame_router.cpp:24`) |
| `rdmOutSelect(int outIdx)` | `src/core/output_init.cpp:29` | RDM command dispatch (caller not in inspected source — see [Open Questions](#18-open-questions)) |
| `dmxIsDelta(int outIdx)` | `src/core/output_init.cpp:113` | `snapshotAndTransmit` (`src/sys/tasks.cpp:98`) |
| `updateOutputRuntime(int outIdx)` | `src/core/output_init.cpp:117` | Web config apply (caller not in inspected source — see [Open Questions](#18-open-questions)) |
| `dmxInitGuardBegin()` | `src/sys/tasks.cpp:43` | `setup()` before `outputInitAll` (`src/main.cpp:106`) |
| `dmxInitGuardEnd()` | `src/sys/tasks.cpp:57` | `setup()` after `outputInitAll` (`src/main.cpp:108`) |

## 8. Data Flow

1. **Config loaded** (core 0, `setup()`): `cfgcore::load()` populates `cfg.outputs[i]` from template + NVS (`src/main.cpp:46`).
2. **Sanitize**: `sanitizeOutputs()` disables outputs with `txPin < 0` (`src/core/output_init.cpp:8-12`).
3. **Crash guard begin**: `dmxInitGuardBegin()` reads NVS crash counter and progressively disables outputs (`src/sys/tasks.cpp:43-55`).
4. **Init all outputs**: `outputInitAll()` resets all state to defaults (`src/core/output_init.cpp:36-47`):
   - `rdmLineForOut[i] = -1`, `rdmOutForLine[i] = -1`, `outReady[i] = false`, `g_outputs[i].ready = false` (`src/core/output_init.cpp:39-44`).
   - Resets `monitorOut = 0`, `rdmOut = -1` (`src/core/output_init.cpp:46-47`).
5. **Per-output init** (loop, `src/core/output_init.cpp:48-108`):
   - Skip if `!enabled` or `txPin < 0` (`src/core/output_init.cpp:49-54`).
   - Check for RDM UART port duplication (`src/core/output_init.cpp:55-58`).
   - Resolve mode via `resolveOutputMode(cfg.outputs[i].mode, cfg.outputs[i].rtsPin)` (`src/core/output_init.cpp:64`).
   - Map UART: port 1 → `UART_NUM_1`, port 2 → `UART_NUM_2` (`src/core/output_init.cpp:66-69`).
   - Initialize RMT: `rmtDmxInit(&g_outputs[i].rmt, txPin, rmtCh)` where `rmtCh = i` (`src/core/output_init.cpp:70-71`).
   - On success: populate `g_outputs[i]` fields from config (`src/core/output_init.cpp:72-83`).
   - Set `outReady[i] = true`, `dmxReady = true` (`src/core/output_init.cpp:82`).
   - Set `monitorOut` to first enabled output (`src/core/output_init.cpp:83`).
   - If `inputMode != DMX_IN_OFF && rxPin >= 0`: init DMX input via `dmxInInit()` (`src/core/output_init.cpp:85-94`).
   - If RDM mode: `rdmRmtInit(&g_outputs[i].rmt, rtsPin, rxPin, uart)` → map line via `rdmLineForOut[i]`/`rdmOutForLine[line]` (`src/core/output_init.cpp:95-103`).
6. **Crash guard end**: `dmxInitGuardEnd()` writes incremented counter to NVS, waits 3 s, resets counter if stable (`src/sys/tasks.cpp:57-76`).
7. **Runtime**: `dmxTxTask` on core 1 checks `outReady[i]` and `cfg.outputs[i].enabled` per tick (`src/sys/tasks.cpp:128`), reads `g_outputs[i].rmt` for `rmtDmxIdle`/`rmtDmxKick` (`src/sys/tasks.cpp:101-106`).
8. **Live config**: `updateOutputRuntime(outIdx)` re-applies `breakTime`/`mabTime`/`invert` to `g_outputs[outIdx].rmt` (`src/core/output_init.cpp:117-123`).

## 9. Protocol Layout

N/A (no wire protocol). The output init module configures the physical DMX line (break/MAB timing, start code slot, DE/RE GPIO) but does not itself transmit or receive wire packets. The DMX frame wire format is documented in `[drv-dmx-rmt-tx](./drv-dmx-rmt-tx.md)` and the RDM frame format in `[core-rdm-engine](./core-rdm-engine.md)`.

## 10. Config Integration

Reads (all from `Config.outputs[]`, `include/config_schema.h:38-95`):

| Field | CFG flag | Source line (config_schema.cpp) | Read in (output_init.cpp) |
|---|---|---|---|
| `enabled` | `CFG_REBOOT` | `src/cfg/config_schema.cpp:154` | `sanitizeOutputs` (line 10), `outputInitAll` (line 49) |
| `txPin` | `CFG_REBOOT` | `src/cfg/config_schema.cpp:161` | `sanitizeOutputs` (line 10), `outputInitAll` (line 50, 71) |
| `port` | `CFG_REBOOT` | `src/cfg/config_schema.cpp:160` | `outputInitAll` (line 65-69) |
| `rtsPin` | `CFG_REBOOT` | `src/cfg/config_schema.cpp:163` | `outputInitAll` (line 57-58, 64, 96) |
| `rxPin` | `CFG_REBOOT` | `src/cfg/config_schema.cpp:162` | `outputInitAll` (line 85, 96) |
| `mode` | `CFG_REBOOT` | `src/cfg/config_schema.cpp:171` | `outputInitAll` (line 64) |
| `universe` | `CFG_LIVE` | `src/cfg/config_schema.cpp:155` | `outputInitAll` (line 104) — logged only |
| `breakTime` | `CFG_LIVE` | `src/cfg/config_schema.cpp:172` | `outputInitAll` (line 79), `updateOutputRuntime` (line 120) |
| `mabTime` | `CFG_LIVE` | `src/cfg/config_schema.cpp:173` | `outputInitAll` (line 80), `updateOutputRuntime` (line 121) |
| `invert` | `CFG_LIVE` | `src/cfg/config_schema.cpp:174` | `outputInitAll` (line 81), `updateOutputRuntime` (line 122) |
| `inputMode` | `CFG_LIVE` | `src/cfg/config_schema.cpp:175` | `outputInitAll` (line 85) |
| `txStyle` | `CFG_LIVE` | `src/cfg/config_schema.cpp:169` | Not read here — read in `tasks.cpp:98` and `merge_engine.cpp` |

PIN/GPIO/driver-bound settings (`txPin`, `rxPin`, `rtsPin`, `port`, `mode`) require a reboot (`CFG_REBOOT`). Signal-level settings (`breakTime`, `mabTime`, `invert`, `inputMode`) apply live via `updateOutputRuntime()`. Writes: none — this module never writes config fields.

## 11. Lifecycle

- **Config load + sanitize** (core 0, `setup()`): `cfgcore::load()` → `sanitizeOutputs()` (`src/main.cpp:46-47`).
- **Crash guard + init** (core 0, `setup()`): `dmxInitGuardBegin()` → `outputInitAll()` → `dmxInitGuardEnd()` (`src/main.cpp:106-108`).
- **Runtime (core 1)**: `dmxTxTask` per 1 ms tick checks `outReady[i]` and reads `g_outputs[i]` (`src/sys/tasks.cpp:97-106`).
- **Live config apply**: `updateOutputRuntime(outIdx)` called when `breakTime`/`mabTime`/`invert` are changed via web UI or serial (`/config` endpoint applies live — see `core-config-engine` module).
- **Shutdown**: None. RMT channels and UARTs are never deinitialized at runtime — see `[drv-dmx-rmt-tx](./drv-dmx-rmt-tx.md):194-196` for the rationale (RMT channel is never released because RDM uses it).

## 12. Error Handling

- `sanitizeOutputs()`: no return — silently disables output (`cfg.outputs[i].enabled = false`) if `txPin < 0` (`src/core/output_init.cpp:10-11`).
- `outputInitAll()`: returns `void`. On RMT init failure (`rmtDmxInit` returns `false`), logs `"outN RMT init FAILED"` and leaves `outReady[i] = false` (`src/core/output_init.cpp:107`). No crash.
- `outputInitAll()`: RDM UART port duplication check — logs `"outN skipped: RDM UART port N already in use"` and skips (`src/core/output_init.cpp:59-62`).
- `outputInitAll()`: missing TX pin (after sanitize) — logs `"outN skipped: enabled but no TX pin"` (`src/core/output_init.cpp:50-53`).
- `outputInitAll()`: no outputs enabled — logs `"no outputs enabled"` (`src/core/output_init.cpp:109`).
- `updateOutputRuntime()`: silently no-ops if `outIdx` is out of range or `!outReady[outIdx]` (`src/core/output_init.cpp:118-119`).
- All logging via `Serial.printf` (`src/core/output_init.cpp:51,60,87,91,104,109,110`).

## 13. Memory Allocation

- `g_outputs[MAX_OUTPUTS]` is a **static DRAM** array at `src/core/output_init.cpp:14`. Size: 4 × `sizeof(dmx_output_t)`. Each `dmx_output_t` contains a `RmtDmx` struct (`src/drv/dmx_rmt.h:31`) whose `sym` field is a **separate heap allocation** via `heap_caps_malloc(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)` at `src/drv/dmx_rmt.h:112-113` — up to `RMT_DMX_MAX_SYM` (3000) `rmt_symbol_word_t` entries (~12 KB per output).
- `outReady[]`, `rdmLineForOut[]`, `rdmOutForLine[]` are static DRAM arrays (`src/core/output_init.cpp:15,19,20`).
- No dynamic allocation in `output_init.cpp` itself — all heap comes from `rmtDmxInit` in the driver.
- Total static DRAM for this module: ~4 × 32 bytes (dmx_output_t bare) ≈ 128 bytes. The RMT symbol buffers are driver-layer allocation.

## 14. Timing

- `outputInitAll()` runs once at boot; the per-output loop (4 iterations max) includes `rmtDmxInit` which does RMT channel config + encoder creation + a priming transmit + `rmt_tx_wait_all_done(200ms)` (`src/drv/dmx_rmt.h:158-166`). Per output: ~23 ms (one DMX frame) for priming wait. Total worst case for 4 outputs: ~92 ms (all outputs prime sequentially, no parallelization).
- `dmxInitGuardEnd()` blocks for 3000 ms (`src/sys/tasks.cpp:62-65`) — this delays `setup()` completion but ensures stability.
- `dmxIsDelta()` is O(1) — single struct member comparison (`src/core/output_init.cpp:114`).
- `updateOutputRuntime()` is O(1) — three field copies (`src/core/output_init.cpp:120-122`).
- `viewOutput()` is O(4) worst case — falls back to linear scan of enabled outputs (`src/core/output_init.cpp:23-27`).
- No hard deadline at runtime — all runtime functions are sub-microsecond except the init path.

## 15. Traceability

| Claim | Evidence |
|---|---|
| `sanitizeOutputs` disables outputs with `txPin < 0` | `src/core/output_init.cpp:10-11` |
| `g_outputs`, `outReady`, `dmxReady`, `monitorOut`, `rdmOut` are extern globals | `src/core/output_init.h:6-10` |
| `rdmLineForOut`, `rdmOutForLine` are extern globals | `src/core/output_init.h:11-12` |
| `rdmLineForOut` initialized to {-1,-1,-1,-1} | `src/core/output_init.cpp:19` |
| `rdmOutForLine` initialized to {-1,-1,-1,-1} | `src/core/output_init.cpp:20` |
| `viewOutput` returns `monitorOut` if enabled, else first enabled, else 0 | `src/core/output_init.cpp:22-27` |
| `rdmOutSelect` calls `rdmRmtSelect(line)` | `src/core/output_init.cpp:31-33` |
| `outputInitAll` resets all state at the top | `src/core/output_init.cpp:36-46` |
| `monitorOut` set to first enabled output | `src/core/output_init.cpp:83` |
| RMT init uses `rmtCh = i` (channel per output index) | `src/core/output_init.cpp:70` |
| RMT init success sets `g_outputs[i].ready = true`, `outReady[i] = true`, `dmxReady = true` | `src/core/output_init.cpp:72-82` |
| UART port mapping: port 1 → UART_NUM_1, port 2 → UART_NUM_2 | `src/core/output_init.cpp:66-69` |
| `rmt.chan` and `rmt.sym` set to nullptr at init start (for re-init safety) | `src/core/output_init.cpp:71,43-44` |
| RMT break/mab/invert applied from config at init | `src/core/output_init.cpp:79-81` |
| DMX input init guarded by `inputMode != DMX_IN_OFF && rxPin >= 0` | `src/core/output_init.cpp:85` |
| RDM line init guarded by `mode == OUTPUT_MODE_RDM_FULL` | `src/core/output_init.cpp:95` |
| `rdmLineForOut[i]` / `rdmOutForLine[line]` bidirectional mapping built | `src/core/output_init.cpp:99-100` |
| `rdmOut` set to first RDM-capable output | `src/core/output_init.cpp:101` |
| `dmxIsDelta` checks `txStyle == TXSTYLE_DELTA` | `src/core/output_init.cpp:114` |
| `updateOutputRuntime` updates break/mab/invert from config | `src/core/output_init.cpp:120-122` |
| `resolveOutputMode` returns RDM_FULL if modeVal==1 or rtsPin>=0 | `include/output.h:29-30` |
| Crash guard begin disables outputs from crash count down | `src/sys/tasks.cpp:48-54` |
| Crash guard end: 3 s stable window, NVS reset | `src/sys/tasks.cpp:57-76` |
| Call sequence: dmxInitGuardBegin → outputInitAll → dmxInitGuardEnd | `src/main.cpp:106-108` |
| `snapshotAndTransmit` checks `outReady[i]` before snapshotting | `src/sys/tasks.cpp:97` |
| `dmxFrameTick` checks `cfg.outputs[i].enabled && outReady[i]` | `src/sys/tasks.cpp:128` |

## 16. Cross-References

- `[drv-dmx-rmt-tx](./drv-dmx-rmt-tx.md)` — `rmtDmxInit()` initializes the RMT TX channel, symbol buffer, and priming frame (`src/drv/dmx_rmt.h:109-167`). `outputInitAll` calls it at `src/core/output_init.cpp:71`.
- `[drv-dmx-uart-rx](./drv-dmx-uart-rx.md)` — `dmxInInit()` configures the UART for DMX input mode (`src/drv/dmx_input.h:24`). Called from `src/core/output_init.cpp:86` when `inputMode != DMX_IN_OFF` (`include/config_enums.h:7-9`).
- `[core-rdm-engine](./core-rdm-engine.md)` — `rdmRmtInit()` registers an RDM line; `rdmRmtSelect()` switches the active line (`src/core/rdm_engine.h:57,60`). Called from `src/core/output_init.cpp:32,96`.
- `[core-rdm-task](./core-rdm-task.md)` — consumes `rdmOut` and `rdmLineForOut` at runtime (`src/core/output_init.cpp:18` global).
- `[core-frame-router](./core-frame-router.md)` — calls `viewOutput()` for monitor logging (`src/core/frame_router.cpp:24`).
- `sys-tasks` — `snapshotAndTransmit` reads `outReady[]` and `g_outputs[i].rmt` (`src/sys/tasks.cpp:97-106`); `dmxFrameTick` checks `cfg.outputs[i].enabled && outReady[i]` (`src/sys/tasks.cpp:128`); `dmxIsDelta` checked at `src/sys/tasks.cpp:114` and `src/sys/tasks.cpp:129`.
- `core-config-engine` — `cfgcore::load()` populates `cfg.outputs[]` before `sanitizeOutputs` (`src/main.cpp:46-47`).
- `sys-crash-guard` — `dmxInitGuardBegin/End` wrap `outputInitAll` (`src/main.cpp:106-108`, `src/sys/tasks.cpp:43-76`).

## 17. Limitations

- The `rdmLineForOut[]` / `rdmOutForLine[]` mapping arrays are sized `MAX_OUTPUTS` (4) but `RDM_MAX_LINES` is 2 (`src/core/rdm_engine.h:13`) — only 2 RDM lines can exist, so outputs beyond the first two RDM-capable ones map to the same line (the init loop silently overwrites at `src/core/output_init.cpp:99-100`).
- `rmtCh = i` assigns RMT channel number equal to the output index (`src/core/output_init.cpp:70`), but the ESP32-S3 has only 4 RMT TX channels (`include/config_schema.h:93-94`). With `MAX_OUTPUTS=4` this is fine, but the `#warning` at `include/config_schema.h:94` fires if `MAX_OUTPUTS > 4`.
- The RMT symbol buffer (`rd->sym`) is `heap_caps_malloc`'d inside `rmtDmxInit` but never freed on init failure — if `rmt_new_tx_channel` fails on the non-DMA fallback path, the allocated `sym` buffer leaks (`src/drv/dmx_rmt.h:112-143`).
- The `seq` field in `dmx_output_t` (`include/output.h:25`) is declared `volatile` but never read or written by the inspected runtime path — it is dead state.
- UART port duplication is checked only for RDM mode (`src/core/output_init.cpp:55-59`); non-RDM outputs sharing a UART port are not detected (but non-RDM outputs don't use UART RX, so this is benign).

## 18. Open Questions

1. Not determinable from the inspected source code — which function calls `rdmOutSelect(int outIdx)` at runtime. It is declared at `src/core/output_init.h:18` and defined at `src/core/output_init.cpp:29`, but its sole caller is not found in the inspected files. Likely called from the RDM task or Art-Net RDM bridge, but the exact call site must be verified in `src/core/rdm_task.cpp` or `src/net/artnet.cpp`.
2. Not determinable from the inspected source code — which function calls `updateOutputRuntime(int outIdx)`. It is declared at `src/core/output_init.h:19` and defined at `src/core/output_init.cpp:117`, but its caller (likely a web config apply handler in `src/cfg/config_core.cpp` or `src/net/web_routes.cpp`) was not inspected.
3. Not determinable from the inspected source code — whether the `volatile uint32_t seq` field in `dmx_output_t` (`include/output.h:25`) is read by any task or is purely vestigial. No read site was found in the inspected files.
4. Not determinable from the inspected source code — whether there is a runtime path that re-initializes a single output's RMT channel (e.g., after a pin config change), given that `rmtDmxInit` preserves `rd->sym` across deinit/re-init (`src/drv/dmx_rmt.h:111`).

## 19. Testing

- No dedicated `output_init_test.cpp` host test exists — `outputInitAll()` requires RMT driver and UART HAL initialization that cannot be mocked with the current `test/native/shim/` shims.
- `test/native/merge_test.cpp` does not test output init — it calls `updateSender` + `mergeOutput` directly, bypassing the router and init layers (`test/native/merge_test.cpp:36-37`).
- `src/core/output_init.cpp:113-115` (`dmxIsDelta`) is a trivial wrapper that could be unit-tested but is not (no test references `dmxIsDelta` in the inspected test files).
- `resolveOutputMode()` (`include/output.h:28-32`) is a pure function with no test coverage in the inspected files.
- Hardware testing of `outputInitAll` is performed during the 5-minute firmware evaluation workflow (`CLAUDE.md` "Firmware Evaluation Workflow"), which monitors serial output for `"[DMX] outN ready"` and `"[DMX] ready (monitor=outN rdm=outN)"` messages.
- The crash guard (`dmxInitGuardBegin/End`) is exercised by inducing a panic during `outputInitAll` and observing progressive output disablement across reboots — this is a manual hardware test, not automated.

## 20. History

- Crash guard (`dmxInitGuardBegin/End`) moved to `src/sys/tasks.cpp` but wraps `outputInitAll()` — originally part of the output init module before the 5-layer refactor (`src/main.cpp:106-108`).
- `sanitizeOutputs()` extracted as a separate pre-init pass to disable outputs with invalid pins before the crash guard runs (`src/main.cpp:47`).
- `rdmLineForOut` / `rdmOutForLine` bidirectional mapping added to support dynamic RDM line selection across multiple RDM-capable outputs (`src/core/output_init.cpp:19-20,99-100`).
- `splitMask` and `loopback` fields added to `DmxOutput` (`include/config_schema.h:34,35`) for universe mirroring — consumed by `[core-frame-router](./core-frame-router.md):26-36`.
- `updateOutputRuntime()` added to support `CFG_LIVE` application of break/MAB/invert timing without reboot (`src/core/output_init.cpp:117-124`).
- `viewOutput()` added to support WebSocket/Serial monitor display of a specific output's frame (`src/core/frame_router.cpp:24`).
