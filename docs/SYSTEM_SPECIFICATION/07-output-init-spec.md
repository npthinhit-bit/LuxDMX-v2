# Output Init — System Specification

**Domain:** core.output-init
**Spec ID:** SYS-SPEC-07

---

## 1. Module Overview

The Output Init module owns runtime initialization of DMX output channels on the gateway device. It translates the persisted, user-edited configuration (per-output pin assignments, mode, timing) into live runtime structures that the transmit task consumes every 1 ms.

Its responsibilities are:

- **Output sanitization** — disable any output whose TX pin is invalid before hardware init proceeds.
- **RMT TX channel allocation** — allocate one RMT peripheral transmit channel per output and prime the line.
- **UART RX provisioning** — optionally instantiate the DMX-input converter-mode UART and the RDM E1.20 response UART, each on a dedicated port.
- **DE/RE GPIO configuration** — configure half-duplex transceiver direction control for RDM-capable outputs.
- **RDM line mapping** — build a bidirectional mapping between logical outputs and the limited pool of RDM lines.
- **Crash-guard-wrapped init** — sequence init so that a panic during hardware setup progressively disables outputs across reboots until the device is stable.
- **Live config application** — re-apply break/MAB/invert timing to an already-initialized output without a reboot.

The module is the bridge between the **drv** layer (RMT TX, UART RX) and the **core/sys** layers (DMX transmit task, RDM controller task). It populates the runtime `g_outputs[]` table at boot; this table is then read-only by the core-1 transmit task.

---

## 2. External Interfaces

### 2.1 Public Entry Points

| Entry point | Visibility | Direction | Purpose |
|---|---|---|---|
| `sanitizeOutputs()` | external (called from `main.cpp`) | input → config | Disables outputs with invalid TX pins before crash-guard init. |
| `outputInitAll()` | external (called from `main.cpp`) | output → sys/tasks | Initializes all enabled outputs: RMT, UART RX, RDM lines, line mappings. |
| `viewOutput()` | external (called from `core-frame-router`) | query | Returns the output index selected for monitor/serial display. |
| `rdmOutSelect(int outIdx)` | external (called from RDM dispatch) | control | Maps an output index to its RDM line and selects it as active. |
| `dmxIsDelta(int outIdx)` | external (called from `dmxTxTask`) | query | Returns true if an output is in delta (one-frame-per-packet) TX style. |
| `updateOutputRuntime(int outIdx)` | external (called from config apply path) | control | Re-applies live signal-level config (break/MAB/invert) to an output. |
| `dmxInitGuardBegin()` | external (called from `main.cpp`) | control | Reads crash counter; progressively disables outputs from highest index. |
| `dmxInitGuardEnd()` | external (called from `main.cpp`) | control | Writes incremented counter; enforces 3 s stable-window; resets counter if stable. |

### 2.2 Runtime Globals (consumed by other modules)

| Symbol | Type | Producer | Consumer | Description |
|---|---|---|---|---|
| `g_outputs` | `dmx_output_t[4]` | output_init | sys/tasks (`dmxTxTask`) | Per-output runtime state: RMT handle, channel, pins, mode, readiness. |
| `outReady` | `bool[4]` | output_init | sys/tasks | Per-output readiness flag (RMT init succeeded). |
| `dmxReady` | `bool` | output_init | sys/tasks | True when ≥1 output ready. |
| `monitorOut` | `int` | output_init | core-frame-router | Index of output shown in monitor display. |
| `rdmOut` | `int` | output_init | core-rdm-task | First RDM-capable output index, or -1. |
| `rdmLineForOut` | `int[4]` | output_init | core-rdm-engine / core-rdm-task | Output → RDM line index (-1 if none). |
| `rdmOutForLine` | `int[4]` | output_init | core-rdm-engine | RDM line → output index (-1 if none). |

### 2.3 Driver-Layer Dependencies

| Driver function | Interface header | Invoked by |
|---|---|---|
| `rmtDmxInit(rmt, txPin, rmtChannel)` | `src/drv/dmx_rmt.h` | `outputInitAll` |
| `dmxInInit(rxPin, uartPort)` | `src/drv/dmx_input.h` | `outputInitAll` (conditional) |
| `rdmRmtInit(rmt, rtsPin, rxPin, uartPort)` | `src/core/rdm_engine.h` | `outputInitAll` (RDM path) |
| `rdmRmtSelect(lineIdx)` | `src/core/rdm_engine.h` | `rdmOutSelect` |

### 2.4 Configuration Inputs

All inputs are read from the `Config.outputs[i]` table (populated by `cfgcore::load()` from board-template + NVS overlay):

| Field | Type | Flag | Used for |
|---|---|---|---|
| `enabled` | bool | REBOOT | Filtering — skip disabled outputs entirely. |
| `txPin` | int | REBOOT | RMT TX hardware pin. |
| `rxPin` | int | REBOOT | UART RX pin (DMX input / RDM response). |
| `rtsPin` | int | REBOOT | DE/RE GPIO pin (RDM line direction control). |
| `port` | int | REBOOT | UART peripheral index (1 → UART1, 2 → UART2). |
| `mode` | int | REBOOT | Explicit output mode (0 = DMX-only, 1 = RDM-full). |
| `universe` | int | LIVE | Logged only. |
| `breakTime` | int | LIVE | RMT break duration (init + live apply). |
| `mabTime` | int | LIVE | RMT mark-after-break duration (init + live apply). |
| `invert` | bool | LIVE | RMT line inversion (init + live apply). |
| `inputMode` | enum | LIVE | DMX input enable when ≠ OFF and rxPin valid. |
| `txStyle` | enum | LIVE | Delta vs. continuous TX (read by dmxTxTask, not here). |

### 2.5 Outputs / Side Effects

- Populates the runtime globals (table 2.2).
- Allocates one RMT TX channel per output (channel index = output index, 0–3).
- Allocates a heap-backed RMT symbol buffer per output (~12 KB each, internal/DMA-capable).
- Optionally allocates a UART peripheral for DMX input (converter mode).
- Optionally allocates a UART peripheral for RDM response RX + DE/RE GPIO.
- Writes a crash-count NVS key under the `dmxgw` namespace.

---

## 3. State Machine

The module itself is **stateless** after boot — `outputInitAll` is a one-shot init pass that runs once during `setup()`. The stateful component is the **crash guard**, an NVS-backed counter with three states:

| State | Condition | Guard-Begin Behavior | Guard-End Behavior |
|---|---|---|---|
| **Clean boot** | crash counter == 0 | All outputs enabled; normal init proceeds. | Counter incremented to 1; 3 s stable window observed. If stable, counter reset to 0. |
| **Crash recovery** | crash counter == N (>0) | Outputs at indices `N-1` down to `0` are disabled before init. Progressive: each crash disables one more (highest indexed) output. | Counter incremented to N+1; 3 s window; if stable, reset to 0. |
| **Stable** | survived 3 s after guard-end | — | Counter reset to 0. A bad-pin output that was never the cause of a crash is unaffected. |

The crash guard ensures a single misbehaving output pin can never brick the device: each successive boot disables the next-highest output until the device boots cleanly for 3 seconds, at which point the counter clears and the next panic starts the cycle again from the remaining enabled set.

---

## 4. Data Flow

1. **Config load (core 0):** `cfgcore::load()` resolves `Config.outputs[i]` from neutral defaults → board template → saved NVS values.
2. **Sanitize (core 0):** `sanitizeOutputs()` sets `enabled = false` for any output with `txPin < 0`. Outputs disabled here are invisible to all later stages.
3. **Crash guard begin (core 0):** `dmxInitGuardBegin()` reads the NVS crash counter and disables the highest-indexed outputs accordingly.
4. **Init all (core 0):** `outputInitAll()` clears all runtime globals to defaults, then for each enabled output:
   - Validates RDM UART port uniqueness (skips duplicate).
   - Resolves the output mode via `resolveOutputMode(mode, rtsPin)`.
   - Initializes the RMT TX channel (`rmtDmxInit`).
   - On RMT success, populates `g_outputs[i]` from config (pins, mode, UART port).
   - Conditionally initializes DMX input (`dmxInInit`) when input mode is active and an RX pin is present.
   - If RDM-full mode: initializes the RDM line (`rdmRmtInit`) and records the bidirectional `rdmLineForOut`/`rdmOutForLine` mapping; records the first RDM-capable output in `rdmOut`.
5. **Crash guard end (core 0):** `dmxInitGuardEnd()` writes the incremented counter, blocks 3 s, and clears the counter if the device remained stable.
6. **Runtime (core 1):** `dmxTxTask` checks `outReady[i]` + `cfg.outputs[i].enabled` per 1 ms tick; reads `g_outputs[i].rmt` for idle-kick transmission.
7. **Live config change (async):** `updateOutputRuntime(outIdx)` re-applies `breakTime`/`mabTime`/`invert` to `g_outputs[outIdx].rmt` without reboot.

---

## 5. Configuration Integration

The module consumes the `Config.outputs[]` array but never writes back to it. Configuration fields are partitioned by their reload behavior:

**Reboot-required fields (CFG_REBOOT)** — applied only at boot via `outputInitAll`:
`enabled`, `txPin`, `rxPin`, `rtsPin`, `port`, `mode`

**Live-applied fields (CFG_LIVE)** — re-applied without reboot:
`breakTime`, `mabTime`, `invert`, `inputMode`

At boot, all five live fields are staged from config. After boot, only `breakTime`/`mabTime`/`invert` are re-applied on demand via `updateOutputRuntime`; `inputMode` changes affect the DMX input converter and are not hot-applied by this module.

Resolution order: neutral value → active board template → saved NVS value (enforced by `cfgcore::load()` before this module runs).

---

## 6. Lifecycle

| Phase | Timing | Core | Action |
|---|---|---|---|
| **Config load + sanitize** | `setup()` phase 2 | core 0 | `cfgcore::load()` → `sanitizeOutputs()` |
| **Crash guard begin** | `setup()` phase 7 start | core 0 | `dmxInitGuardBegin()` reads + applies crash counter |
| **Output init** | `setup()` phase 7 | core 0 | `outputInitAll()` — RMT/UART/RDM init + line mapping |
| **Crash guard end** | `setup()` phase 7 end | core 0 | `dmxInitGuardEnd()` writes counter, 3 s hold, reset if stable |
| **Task creation** | `setup()` phase 8 | core 0 | `createTasks()` spawns `dmxTxTask` (core 1) which begins reading `g_outputs[]` |
| **Runtime (steady state)** | every 1 ms | core 1 | `dmxTxTask` checks `outReady[i]` and transmits; `dmxIsDelta` evaluated per tick |
| **Live config apply** | on-demand | core 0 (caller context) | `updateOutputRuntime(outIdx)` — sub-µs field re-apply |
| **Shutdown** | never | — | No deinit path. RMT channels and UARTs are intentionally never released (the RMT TX peripheral is reused by RDM). |

---

## 7. Error Handling

| Condition | Behavior | Recovery |
|---|---|---|
| `txPin < 0` on a supposedly-enabled output | `sanitizeOutputs` sets `enabled = false` silently. | User correction via web/serial config; resolved on next boot. |
| RMT init failure (`rmtDmxInit` returns false) | Logs `outN RMT init FAILED`; leaves `outReady[i] = false`; continues to next output. | Bad pin or hardware fault — output skipped; other outputs proceed. |
| Duplicate RDM UART port across outputs | Logs `outN skipped: RDM UART port N already in use`; skips the second output's RDM init. | User correction of `port` field in config. |
| Enabled output with no TX pin (post-sanitize) | Logs `outN skipped: enabled but no TX pin`; skips. | Output remains disabled. |
| No outputs enabled at all | Logs `no outputs enabled`; `dmxReady` stays false. | Device boots with no DMX transmission; web config still available. |
| `updateOutputRuntime` called with invalid index or unready output | Silently no-ops (range-guard check). | No effect; caller must re-validate. |
| Panic during init | Crash guard counter increments; next boot disables one more output from the top. | Self-healing across reboots until a stable 3 s window is achieved. |

All diagnostic messages are emitted via `Serial.printf`. No function returns an error code — the module degrades gracefully by leaving outputs in a non-ready state.

---

## 8. Timing Constraints

| Operation | Worst-case latency | Notes |
|---|---|---|
| Per-output RMT init (incl. prime transmit + 200 ms wait) | ~23 ms (one DMX frame period) | `rmt_tx_wait_all_done(200ms)` blocks during priming. |
| Full `outputInitAll` for 4 outputs | ~92 ms | Sequential, not parallelized. |
| `dmxInitGuardEnd` stable window | 3000 ms | Blocks `setup()` completion; non-preemptible. |
| `dmxIsDelta` | sub-µs | Single struct member comparison. |
| `updateOutputRuntime` | sub-µs | Three field copies. |
| `viewOutput` | sub-µs | O(4) fallback scan of `enabled` flags. |
| Runtime tick consumption (core 1) | sub-µs | `outReady[i]` check + `g_outputs[i].rmt` read per 1 ms tick. |

**Boot-time impact:** The 3 s crash-guard hold dominates boot latency. The ~92 ms RMT priming is negligible by comparison. The module imposes no runtime deadline on the 1 kHz DMX tick path.

---

## 9. Memory & Allocation Model

### Static allocation (this module)

| Array | Element size | Count | Total | Location |
|---|---|---|---|---|
| `g_outputs` | `sizeof(dmx_output_t)` ≈ 32 B | 4 | ~128 B | DRAM (static) |
| `outReady` | 1 B | 4 | 4 B | DRAM (static) |
| `rdmLineForOut` | 4 B | 4 | 16 B | DRAM (static) |
| `rdmOutForLine` | 4 B | 4 | 16 B | DRAM (static) |

Scalar globals (`dmxReady`, `monitorOut`, `rdmOut`): ~12 B additional.

### Dynamic allocation (driver layer)

Each RMT TX output allocates a heap-backed symbol buffer via `heap_caps_malloc(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)`, sized to `RMT_DMX_MAX_SYM` (3000) × `rmt_symbol_word_t` entries — approximately **12 KB per output**. In production with 4 outputs this is ~48 KB of internal/DMA-capable heap consumed during init.

The module itself performs no dynamic allocation; all heap originates from `rmtDmxInit` in the driver layer.

**Leak note:** On RMT init failure the symbol buffer is not freed — it is allocated before `rmt_new_tx_channel` is attempted, and a channel-creation failure leaves the buffer orphaned for that boot cycle.

---

## 10. Safety Considerations

1. **Crash guard (progressive output disable):** If `outputInitAll` panics — e.g., from a mis-wired or shorted TX pin — the NVS-backed crash counter ensures the next boot disables the highest-indexed output. After enough cycles all faulty outputs are excluded, leaving the device bootable. A stable 3-second window resets the counter, so a one-off panic is not permanently penalized.

2. **Pre-sanitize pin validation:** `sanitizeOutputs()` runs before the crash guard, ensuring outputs with an explicitly-invalid (`txPin < 0`) pin never reach hardware init — eliminating a class of spurious panics.

3. **RMT failure containment:** A failed `rmtDmxInit` on one output does not abort the init of subsequent outputs. The failed output is simply marked `outReady[i] = false` and skipped by the transmit task.

4. **UARTR port deduplication:** Two RDM-capable outputs claiming the same UART port are detected and the duplicate is skipped, preventing a driver-level conflict panic.

5. **No runtime deinit:** RMT channels and UARTs are never released at runtime by design — the TX RMT channel is shared with RDM, and deinit/re-init would risk timing races on the live DMX bus.

6. **`MAX_OUTPUTS` guard:** A compile-time `#warning` fires if `MAX_OUTPUTS > 4`, since the ESP32-S3 exposes only 4 RMT TX channels and the channel-to-output mapping is identity (`rmtCh = i`).

---

## 11. Cross-Module Dependencies

| Module | Depends on | Via |
|---|---|---|
| **output_init → drv/dmx-rmt-tx** | `rmtDmxInit()` initializes RMT TX channel, symbol buffer, and primes one DMX frame. | `src/drv/dmx_rmt.h:109-167` |
| **output_init → drv/dmx-uart-rx** | `dmxInInit()` configures UART for DMX input converter mode. | `src/drv/dmx_input.h:24` |
| **output_init → core/rdm-engine** | `rdmRmtInit()` registers an RDM line; `rdmRmtSelect()` (via `rdmOutSelect`) switches active line. | `src/core/rdm_engine.h:57,60` |
| **core/frame-router → output_init** | `viewOutput()` selects the monitor output for routing/logging. | `src/core/output_init.h:18` |
| **core/rdm-task → output_init** | Consumes `rdmOut` and `rdmLineForOut` at runtime. | globals in `src/core/output_init.h` |
| **sys/tasks → output_init** | `dmxTxTask` reads `outReady[]`, `g_outputs[i].rmt`, and `cfg.outputs[i].enabled`; checks `dmxIsDelta`. | `src/sys/tasks.cpp:97,98,101,114,128,129` |
| **sys/tasks → output_init (crash guard)** | `dmxInitGuardBegin/End` wrap `outputInitAll()`. | `src/sys/tasks.cpp:43-76` |
| **output_init → cfg/config-engine** | `cfgcore::load()` populates `Config.outputs[]` before sanitize. | `src/main.cpp:46` |
| **output_init → sys/crash-guard** | Crash counter stored in NVS namespace `dmxgw`. | `src/sys/tasks.cpp:40` |

---

## 12. Testing Verification

**No host-native unit test exists for this module.** `outputInitAll()` requires the RMT driver and UART HAL, which the `test/native/shim/` layer does not mock. The module is verified via:

- **Hardware boot validation** — the 5-minute firmware evaluation workflow (`CLAUDE.md`) monitors serial output for `[DMX] outN ready` and `[DMX] ready (monitor=... rdm=...)` messages confirming init success.
- **Crash-guard exercise** — inducing a panic during `outputInitAll` and observing progressive output disablement across reboots (manual).
- **Indirect host coverage** — `test/native/merge_test` exercises the merge engine that *consumes* `g_outputs` but does not call `outputInitAll`, `sanitizeOutputs`, `viewOutput`, `rdmOutSelect`, or `updateOutputRuntime`.

**Uncovered but trivially testable:**
- `resolveOutputMode(modeVal, rtsPin)` — pure function, no host test exists.
- `dmxIsDelta(outIdx)` — single comparison, no host test exists.
- `updateOutputRuntime(outIdx)` — three field copies, no host test exists.

---

## 13. Open Questions

1. **Caller of `rdmOutSelect`** — declared at `src/core/output_init.h:18`, defined at `src/core/output_init.cpp:29`, but the runtime caller (suspected: RDM task dispatch or Art-Net RDM bridge) was not found in inspected sources. Exact call site unverified.

2. **Caller of `updateOutputRuntime`** — declared at `src/core/output_init.h:19`, defined at `src/core/output_init.cpp:117`, but the config-apply caller (suspected: `src/cfg/config_core.cpp` or `src/net/web_routes.cpp`) was not inspected.

3. **`volatile uint32_t seq` field** in `dmx_output_t` — declared but no read site found in inspected files. May be vestigial or intended for future cross-core synchronization.

4. **Single-output re-init path** — whether a runtime path re-initializes one output's RMT channel (e.g., after a pin config change). The driver preserves `rd->sym` across deinit/re-init, suggesting this was considered, but no caller path was confirmed.

---

## 14. History

- **Crash guard relocated** — `dmxInitGuardBegin/End` moved to `src/sys/tasks.cpp` but still wraps `outputInitAll()` (`src/main.cpp:106-108`), retaining original semantics from the 5-layer refactor.
- **Sanitize pre-pass extracted** — `sanitizeOutputs()` split out as a separate `setup()` phase 2 step to disable invalid-pin outputs before the crash guard runs.
- **Bidirectional RDM line mapping added** — `rdmLineForOut[]` / `rdmOutForLine[]` arrays introduced to support dynamic RDM line selection across multiple RDM-capable outputs.
- **Output mode resolution** — `resolveOutputMode()` made an inline predicate in `include/output.h`, returning RDM_FULL when either the explicit mode flag or an RTS pin is present.
- **Live runtime apply** — `updateOutputRuntime()` added to support `CFG_LIVE` application of break/MAB/invert timing without a reboot.
- **Monitor view** — `viewOutput()` added to support WebSocket/Serial monitor display of a specific output's frame.
- **`splitMask` / `loopback` fields** added to the config schema for universe mirroring — consumed by the frame router, not by init directly.

---

*End of specification — 14 sections.*
