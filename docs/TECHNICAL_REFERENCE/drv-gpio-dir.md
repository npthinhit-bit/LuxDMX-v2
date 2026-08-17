# GPIO DE/RE Direction — Technical Reference

Domain: drv.gpio-dir

## 1. Domain Scope

This module controls the **DE/RE (Driver Enable / Receiver Enable) pin** of a half-duplex RS485 transceiver. A single GPIO is wired to both DE and RE (tied together on typical DMX/RDM transceiver modules): `HIGH` = drive (TX enable), `LOW` = listen (RX enable). It is a two-function header-only driver with no internal state and no dependencies beyond the ESP-IDF GPIO driver.

It is consumed exclusively by the **RDM engine** on **core 1**, which toggles DE/RE around RDM request transmission and response listening. The DMX TX path (RMT) runs **uninterrupted** on this pin — the DE/RE GPIO is never switched back to RX during normal DMX output, so DMX framing is never disturbed by RDM bus turnaround (`src/drv/gpio_dir.h:2-6`). This was the fix for issue #64's core requirement that DMX output stay contention-proof under RDM operations.

This module owns nothing else. It does not initialize the RMT channel, configure the UART, or assemble frames — it only sets a GPIO level at the boundaries of an RDM transaction.

## 2. Layer Mapping

| Layer | Module path | Role here |
|---|---|---|
| **drv** | `src/drv/gpio_dir.h` | DE/RE GPIO control for RS485 half-duplex transceiver |
| **cfg** | `src/cfg/config_schema.cpp` | `rtsPin` field (line 163) maps to the DE/RE GPIO; `mode` field (line 171) selects RDM-full vs DMX-only |
| **core** | `src/core/output_init.cpp` | Calls `rdmRmtInit` (line 96) which calls `gpioDeInit` (via `rdm_engine.cpp:53`) |
| **core** | `src/core/rdm_engine.cpp` | Calls `gpioDeInit` (line 53), `gpioDeSet` (lines 102, 107, 135) |
| **core** | `src/core/rdm_disc.cpp` | Calls `gpioDeSet` (line 28) |
| **core** | `src/core/rdm_task.cpp` | Calls `gpioDeSet` (line 101) |
| **core** | `src/core/rdm_engine.h` | `RdmLine.de` (`src/core/rdm_engine.h:25`) stores the DE/RE pin index passed through to `gpioDeInit`/`gpioDeSet` |
| **include** | `include/output.h` | `dmx_output_t.dePin` (line 21) holds the resolved DE/RE pin |

## 3. Source Files

| File | Role |
|---|---|
| `src/drv/gpio_dir.h` | Entire module — two `static inline` functions: `gpioDeInit` (configure + idle-high) and `gpioDeSet` (set level). Header-only, no `.cpp`. |
| `include/output.h` | `dmx_output_t.dePin` (line 21) stores the DE/RE pin; `dmx_output_t.rmt` (line 17) is the RMT context this GPIO gates. |
| `src/core/rdm_engine.h` | `RdmLine.de` (line 25) stores the per-line DE/RE pin; `RdmLine` is populated in `rdmRmtInit` (`rdm_engine.cpp:45-63`). |

## 4. Data Structures

This module defines **no data structures**. It operates directly on integer GPIO pin numbers and the ESP-IDF GPIO driver. All state it influences is held in caller-owned structs:

| Caller struct | Field | Line | What it holds |
|---|---|---|---|
| `RdmLine` | `de` | `src/core/rdm_engine.h:25` | DE/RE GPIO pin index for this RDM line |
| `dmx_output_t` | `dePin` | `include/output.h:21` | DE/RE pin copied from `cfg.outputs[i].rtsPin` at `output_init.cpp:76` |

The pin number flows: `Config.outputs[i].rtsPin` (`config_schema.cpp:163`) → `resolveOutputMode()` returns `OUTPUT_MODE_RDM_FULL` if `rtsPin >= 0` (`include/output.h:30`) → `outputInitAll` calls `rdmRmtInit(&g_outputs[i].rmt, cfg.outputs[i].rtsPin, ...)` (`output_init.cpp:96`) → `rdmRmtInit` stores `dePin` in `RdmLine.de` (`rdm_engine.cpp:57`) → calls `gpioDeInit(dePin)` (`rdm_engine.cpp:53`).

## 5. Concurrency

**Core 1 only.** Every call to `gpioDeInit` and `gpioDeSet` originates from RDM operations on core 1:

| Caller | Core | Priority | Line |
|---|---|---|---|
| `rdmTx()` | Core 1 | 18 | `rdm_engine.cpp:102,107,135` |
| `rdmReadResp()` (via `rdmReadFrame`) | Core 1 | 18 | `rdm_engine.h:19` (9 ms timeout budget) |
| `rdmDiscBranch()` | Core 1 | 18 | `rdm_disc.cpp:28` |
| `rdmRmtInit()` `gpioDeInit` | Core 1 (during `outputInitAll`) | 19 (dmxTxTask) / boot | `rdm_engine.cpp:53`, `output_init.cpp:96` |
| RDM task `RDM_CMD_RAW_RELAY` | Core 1 | 18 | `rdm_task.cpp:101` |

No other core accesses these GPIO pins for DE/RE control. GPIO level writes are not atomic in the hardware sense, but since only core 1 writes them and only during RDM transactions (never from `dmxTxTask`'s 1 ms frame loop), there is no contention and no lock is needed. The `dmxTxTask` (`tasks.cpp:83`) does **not** call `gpioDeSet` — it only calls `rmtDmxKick` (`tasks.cpp:103`), which transmits on the RMT channel while DE/RE stays HIGH (driving) from the most recent `gpioDeSet(de, 1)`.

## 6. State Machine

No state machine — stateless request/response. `gpioDeInit` is called once during initialization to configure the pin as output and set the idle level; `gpioDeSet` is called repeatedly during RDM transactions to toggle between drive (TX) and listen (RX). The "state" (which level the pin is at) lives in the GPIO hardware register, not in this module. There are no entry/exit events beyond the call itself.

## 7. Entry Points

| Entry point | First call site | Line | Caller context |
|---|---|---|---|
| `gpioDeInit(int pin)` | `rdmRmtInit()` | `src/core/rdm_engine.cpp:53` | Called during `outputInitAll` → `rdmRmtInit` (`output_init.cpp:96`) for each RDM-capable output. Configures the pin and sets it idle-HIGH. |
| `gpioDeSet(int pin, int level)` | `rdmTx()` | `src/core/rdm_engine.cpp:102` | Called from RDM transaction path on core 1: `1` (drive) before TX, `0` (listen) after TX. |

All other calls are at `rdm_engine.cpp:107` (post-TX → listen), `rdm_engine.cpp:135` (listen for response), `rdm_disc.cpp:28` (post-disc branch → listen), `rdm_task.cpp:101` (post-RDM-response → drive).

## 8. Data Flow

### RDM transaction DE/RE toggle

1. **`rdmRmtInit(rmt, dePin, rxPin, uart)`** (`src/core/rdm_engine.cpp:45-63`) is called during `outputInitAll` for each output in `OUTPUT_MODE_RDM_FULL` (`output_init.cpp:95-103`).
2. **`gpioDeInit(dePin)`** (`src/drv/gpio_dir.h:9-16`, called at `rdm_engine.cpp:53`): configures the pin as `GPIO_MODE_OUTPUT` (line 12), sets it level `1` (drive/TX) at line 15, with the comment "idle in drive so RMT DMX keeps clocking." This ensures that when the output returns to normal DMX TX, DE/RE is already high.
3. **`rdmTx(pkt, len)`** (`src/core/rdm_engine.cpp:96-110`) executes the RDM request:
   - **`gpioDeSet(g_rdm.de, 1)`** at `rdm_engine.cpp:102` — drive (TX) enable. DE/RE HIGH.
   - Encodes + transmits the RDM request over RMT (lines 103-106).
   - **`gpioDeSet(g_rdm.de, 0)`** at `rdm_engine.cpp:107` — listen (RX) enable. DE/RE LOW.
   - **`esp_rom_delay_us(90)`** (line 108) — transceiver turn-round time.
   - **`uartRxFlush(g_rdm.uart)`** (line 109) — clear stale bytes.
4. **`rdmReadResp()`** (`rdm_engine.cpp:131-175`): after `rdmReadFrame` times out or returns the response, calls **`gpioDeSet(g_rdm.de, 1)`** at `rdm_engine.cpp:135` — back to drive (TX) so DMX output resumes uninterrupted.
5. **`rdmDiscBranch()`** (`src/core/rdm_disc.cpp:27-28`): after reading the discovery response, calls `gpioDeSet(g_rdm.de, 1)` to re-enable TX.

**Key invariant**: Between step 4 and the next RDM transaction, the DE/RE pin stays HIGH. The `dmxTxTask` 1 ms loop continuously calls `rmtDmxKick` (`tasks.cpp:103`) during this window — DMX frames are transmitted normally with DE/RE high, completely uninterrupted by the RDM listen window (`src/drv/gpio_dir.h:5-6`).

## 9. Protocol Layout

N/A (no wire protocol). This module controls a GPIO level (`HIGH`/`LOW`), not a data protocol. The RS485 transceiver's electrical behavior is:

| GPIO level | DE | RE | Transceiver state | DMX direction |
|---|---|---|---|---|
| `1` | High | High | Driver enabled, receiver disabled | TX (drive) |
| `0` | Low | Low | Driver disabled, receiver enabled | RX (listen) |

This matches the standard RS485 DE/RE pinout where both are active-high and tied together for half-duplex.

## 10. Config Integration

This module reads **no** `Config` fields directly. The DE/RE pin originates in config and is resolved by its callers:

| Config field | Resolution path | CFG flags | Live/reboot |
|---|---|---|---|
| `rtsPin` | `Config.outputs[i].rtsPin` → `resolveOutputMode()` returns `OUTPUT_MODE_RDM_FULL` if `rtsPin >= 0` (`include/output.h:30`) → `outputInitAll` passes it to `rdmRmtInit` (`output_init.cpp:96`) | `OINT` (line 163 of `config_schema.cpp`) | `CFG_REBOOT` |
| `mode` | `Config.outputs[i].mode` → `resolveOutputMode()` (`include/output.h:28-29`): `mode == OUTPUT_MODE_RDM_FULL (1)` forces RDM mode even if `rtsPin < 0` | `OENUM_R` (line 171) | `CFG_REBOOT` |
| `port` | `Config.outputs[i].port` → `rdmRmtInit` resolves UART via `RDM_LINE_UART` (`rdm_engine.h:23`) | `OINT` (line 160) | `CFG_REBOOT` |

No live config fields affect DE/RE behavior — the pin and its mode are fixed at init time (`output_init.cpp:95-103`).

## 11. Lifecycle

| Phase | Function | Call site | Notes |
|---|---|---|---|
| Init | `gpioDeInit(int pin)` | `rdm_engine.cpp:53` via `rdmRmtInit` (`output_init.cpp:96`) | Configures GPIO as output, sets idle HIGH (drive). Called once per RDM-capable output during `outputInitAll`. |
| Runtime | `gpioDeSet(int pin, int level)` | `rdm_engine.cpp:102,107,135`; `rdm_disc.cpp:28`; `rdm_task.cpp:101` | Toggles DE/RE around each RDM transaction. Drive (1) before/after TX; listen (0) during response window. |
| Cleanup | (none) | — | No deinit or teardown; the GPIO stays as output until reset. `src/drv/dmx_rmt.h:194` notes that RMT channels are also not released, by design (RDM reuses the DMX TX channel). |

The init sequence in `main.cpp:106-108`:
```
dmxInitGuardBegin();   // main.cpp:106 — crash counter
outputInitAll();        // main.cpp:107 — calls rmtDmxInit + rdmRmtInit/gpioDeInit
dmxInitGuardEnd();      // main.cpp:108 — 3s stable-uptime reset of crash counter
```

## 12. Error Handling

| Return | Line | Condition |
|---|---|---|
| `void` from `gpioDeInit` | `src/drv/gpio_dir.h:9` | Returns early (no-op) if `pin < 0` (line 10). Otherwise configures GPIO and sets level. No error logging — assumes the pin is valid (validated upstream by `resolveOutputMode`/`outputInitAll`). |
| `void` from `gpioDeSet` | `src/drv/gpio_dir.h:18` | No guard on `pin < 0` (line 18-19); calls `gpio_set_level` unconditionally. If called with an invalid pin, the ESP-IDF GPIO driver will return an error silently (no `ESP_LOGE`, no `Serial`). |
| `gpio_config` result | `src/drv/gpio_dir.h:11-14` | Return value of `gpio_config(&io)` is **not checked** (line 14). A failed configuration (invalid pin, wrong mode) is silently ignored. |

No `ESP_LOGE` or `Serial.printf` calls exist in this module (`src/drv/gpio_dir.h:1-20`). Error visibility depends entirely on the caller (`rdm_engine.cpp`, `rdm_disc.cpp`, `rdm_task.cpp`) propagating or logging upstream failures, which they do not — the RDM transport layer returns `bool` from `rdmReadResp` (`rdm_engine.h:68`) but GPIO-level errors are not surfaced.

## 13. Allocation

**None.** This module performs no heap (`malloc`/`heap_caps_malloc`) or stack allocation. The `gpio_config_t io` struct at `src/drv/gpio_dir.h:11` is a 16-byte stack-local initialized to `{}` (zero). No PSRAM is involved. The `pin_bit_mask` field (`src/drv/gpio_dir.h:12`) is a 64-bit bitmask set via `1ULL << pin`.

## 14. Timing

| Constraint | Value | Source | Basis |
|---|---|---|---|
| GPIO set level latency | < 1 µs | `gpio_set_level` (`gpio_dir.h:19`), `src/core/rdm_engine.cpp:135` (`esp_rom_delay_us(90)`) | ESP32 GPIO matrix; negligible vs RDM µs-scale timing |
| DE/RE drive-to-listen settling | Not explicit; RDM engine adds 90 µs | `rdm_engine.cpp:108` (`esp_rom_delay_us(90)`) | Transceiver turn-round time after switching to RX |
| RDM TX→RX turnaround | 90 µs + UART RX wait | `rdm_engine.cpp:108` + `rdmReadFrame` 9 ms budget (`rdm_engine.h:19`) | E1.20 requires response within 44 µs + responder processing; 90 µs is the controller's listen-after-talk delay |
| DE/RE listen-to-drive | Immediate on `gpioDeSet(de, 1)` | `rdm_engine.cpp:135` | Re-enable TX immediately after response read completes |
| DMX uninterrupted window | From `gpioDeSet(de,1)` to next RDM transaction | `tasks.cpp:102-103` (always kick DMX) | DE/RE stays HIGH during all `dmxTxTask` 1 ms ticks between RDM ops |
| `dmxTxTask` tick | 1 ms | `tasks.cpp:142` (`vTaskDelayUntil`) | Core 1, priority 19 — DMX frames sent every 1 ms regardless of RDM state |

The DE/RE toggle timing is governed by the RDM engine (`rdm_engine.cpp`), not by this module. This module only asserts the level change is immediate (raw GPIO write).

## 15. Traceability

| Claim | Source |
|---|---|
| DE/RE GPIO control, HIGH=drive, LOW=listen | `src/drv/gpio_dir.h:2-4` |
| RMT TX channel never released; DMX runs uninterrupted between RDM ops | `src/drv/gpio_dir.h:5-6` |
| `gpioDeInit` configures pin as output, sets idle HIGH | `src/drv/gpio_dir.h:9-16` |
| `gpioDeSet` sets arbitrary level on the DE/RE pin | `src/drv/gpio_dir.h:18-19` |
| `pin < 0` guard in `gpioDeInit` | `src/drv/gpio_dir.h:10` |
| `gpio_config` result not checked | `src/drv/gpio_dir.h:11-14` |
| `gpioDeSet` has no pin-validity guard | `src/drv/gpio_dir.h:18-19` |
| No `ESP_LOGE`/`Serial` in this module | `src/drv/gpio_dir.h:1-20` (no logging calls present) |
| `gpioDeInit` called in `rdmRmtInit` | `src/core/rdm_engine.cpp:53` |
| DE/RE = LOW (listen) before RDM response read | `rdm_engine.cpp:107` (`gpioDeSet(g_rdm.de, 0)`) |
| DE/RE = HIGH (drive) before RDM TX | `src/core/rdm_engine.cpp:102` (`gpioDeSet(g_rdm.de, 1)`) |
| 90 µs turn-round delay | `src/core/rdm_engine.cpp:108` (`esp_rom_delay_us(90)`) |
| UART flush before listen | `src/core/rdm_engine.cpp:109` (`uartRxFlush`) |
| DE/RE = HIGH after response read | `src/core/rdm_engine.cpp:135` (`gpioDeSet(g_rdm.de, 1)`) |
| DE/RE = HIGH after discovery branch | `src/core/rdm_disc.cpp:28` (`gpioDeSet(g_rdm.de, 1)`) |
| DE/RE = HIGH (drive) in RDM task RAW_RELAY branch | `src/core/rdm_task.cpp:101` (`gpioDeSet(g_rdm.de, 1)`) |
| DE/RE pin stored in `RdmLine.de` | `src/core/rdm_engine.h:25` |
| DE/RE pin stored in `dmx_output_t.dePin` (from `rtsPin`) | `include/output.h:21`, `src/core/output_init.cpp:76` |
| `resolveOutputMode`: RDM-full if `rtsPin >= 0` | `include/output.h:30` |
| RDM-capable outputs call `rdmRmtInit` in `outputInitAll` | `src/core/output_init.cpp:95-103` |
| `RDM_LINE_UART` maps line 0→UART2, line 1→UART1 | `src/core/rdm_engine.h:23` |
| `dmxTxTask` does not call `gpioDeSet` (only `rmtDmxKick`) | `src/sys/tasks.cpp:96-108` (no DE/RE call) |
| Init wrapped by crash guard | `src/main.cpp:106-108` (`dmxInitGuardBegin`/`outputInitAll`/`dmxInitGuardEnd`) |
| RDM task: core 1, priority 18, stack 8192 | `src/core/rdm_task.h:12-14`; `rdm_task.cpp:155-157` |
| `dmxTxTask`: core 1, priority 19, stack 8192 | `src/sys/tasks.cpp:83` |
| `netRxTask`: core 0, priority 5 | `src/sys/tasks.cpp:84` |
| `output_mode_t` enum | `include/output.h:11-14` |
| `rtsPin` config field, CFG_REBOOT | `src/cfg/config_schema.cpp:163` |

## 16. Cross-References

| Module doc | Consumes (this module provides) | Provides to that module |
|---|---|---|
| [DMX RMT TX](./drv-dmx-rmt-tx.md) | — | DE/RE GPIO is NOT controlled by RMT TX; this module is the only DE/RE writer. `dmxTxTask` only calls `rmtDmxKick` (`tasks.cpp:103`), never `gpioDeSet`. |
| [DMX UART RX](./drv-dmx-uart-rx.md) | — | UART RX primitive (`uart_rx.h`) is paired with DE/RE GPIO in the RDM engine; this module's `gpioDeSet` wraps the same RDM TX/RX sequence (`rdm_engine.cpp:102-109`). |
| [RDM Engine](./core-rdm-engine.md) | `gpioDeInit` (`rdm_engine.cpp:53`), `gpioDeSet` (`rdm_engine.cpp:102,107,135`) | RMT TX channel context (`RdmLine.rmt`, `rdm_engine.h:25`) that the DE/RE pin gates |
| [RDM Discovery](./core-rdm-discovery.md) | `gpioDeSet` (`rdm_disc.cpp:28`) | — |
| [RDM Task](./core-rdm-task.md) | `gpioDeSet` (`rdm_task.cpp:101`) | — |
| [Output Init](./core-output-init.md) | — | `rdmRmtInit` (line 96) calls `gpioDeInit` via the RDM engine; `dePin` resolved at `output_init.cpp:76` |
| [Include Headers](./include-headers.md) | `dmx_output_t.dePin` (`include/output.h:21`) | `output_mode_t` enum; `RmtDmx` type |
| [Sys Tasks](./sys-tasks.md) | — | Runs `dmxTxTask` (core 1, prio 19) which transmits on the RMT channel that DE/RE gates; DE/RE stays HIGH during DMX TX |

## 17. Limitations

- **No pin-validity guard in `gpioDeSet`**: `gpioDeSet(int pin, int level)` (`src/drv/gpio_dir.h:18-19`) does not check `pin < 0` before calling `gpio_set_level`. If called with an invalid pin (e.g., a misconfigured output with a garbage `rtsPin`), the ESP-IDF will silently error. `gpioDeInit` has the guard (`gpio_dir.h:10`) but `gpioDeSet` does not — an inconsistency.
- **`gpio_config` return value unchecked**: `gpioDeInit` (`src/drv/gpio_dir.h:14`) calls `gpio_config(&io)` and ignores its `esp_err_t` return. A pin that cannot be configured as output (e.g., strapping pin in use) will silently proceed, and the subsequent `gpio_set_level` may also fail silently.
- **No state readback**: there is no `gpioDeGet` or level-read function. The current DE/RE level is implicit in the last `gpioDeSet` call and not queryable. Debugging a bus-direction fault requires external probing.
- **Hard-coded idle level**: `gpioDeInit` always sets idle HIGH (`src/drv/gpio_dir.h:15`). If a board's RS485 transceiver uses active-low DE/RE, this module would drive the wrong idle state. The comment at `gpio_dir.h:4` ("HIGH = drive (TX), LOW = listen (RX)") assumes the standard active-high convention; no inversion flag exists.
- **Single-wire assumption**: the module assumes DE and RE are tied together (`src/drv/gpio_dir.h:2`). Boards that wire DE and RE to separate GPIOs are not supported by this abstraction.
- **No DE/RE transition tracking**: there is no mechanism to detect or log an illegal transition (e.g., `gpioDeSet(de, 0)` called while `dmxTxTask` is mid-transmit). Correctness relies on the RDM engine's call ordering (`rdm_engine.cpp:96-110`), not on this module enforcing it.

## 18. Open Questions

- Not determinable from the inspected source code — whether `gpioDeSet` is ever called with a pin that was not first `gpioDeInit`-ed; the function has no internal guard, so a caller error would silently write to an unconfigured GPIO.
- Not determinable from the inspected source code — whether the 90 µs turn-round delay (`rdm_engine.cpp:108`) is empirically validated against the specific RS485 transceiver used in the LuxDMX hardware, or whether it was copied from the esp_dmx library defaults.
- Not determinable from the inspected source code — whether any board variant uses a transceiver with active-low DE/RE (which would require this module to invert its logic, currently hard-coded as active-high at `gpio_dir.h:3-4,15`).
- Not determinable from the inspected source code — whether the `pin < 0` guard gap in `gpioDeSet` (as opposed to `gpioDeInit` at line 10) was intentional (callers always pre-validate) or an oversight.
- Not determinable from the inspected source code — whether separate DE/RE pins (not tied together) are planned, which would require splitting `gpioDeInit`/`gpioDeSet` into per-signal controls.

## 19. Testing

No test coverage for this domain. No native or unit-test files in `test/native/` or `test/unit-test/` reference `gpioDeInit`, `gpioDeSet`, or `DmxOutput.dePin` / `RdmLine.de`. This module is a pure hardware GPIO wrapper with no shim-compatible test path — GPIO configuration requires real ESP-IDF hardware context (`driver/gpio.h`). The `rdm_types_test` (`test/native/rdm_types_test.cpp`) does not cover GPIO.

## 20. History

- **Issue #64** (core separation): the DE/RE GPIO was introduced so that RDM could toggle transceiver direction without releasing the RMT TX channel. Comment at `src/drv/gpio_dir.h:5-6` documents the design constraint: "the RMT TX channel is never released — DMX output on this pin is uninterrupted between RDM transactions." This prevents the original issue-64 corruption where core-0 network DMA delayed break timing.
- **RDM architecture (dispatch vs low-level)**: `rdm_engine.h:2` documents that "RDM requests go out over RMT (same channel as DMX), responses come back on a RX-only UART, DE/RE is a GPIO." The `gpio_dir.h` module implements exactly the DE/RE half of that triad. The project decision `rdm_architecture_dispatch_vs_lowlevel` (`project.md`) notes that the RDM engine uses low-level primitives (`gpioDeSet`, `rmtDmxEncode`, `uartRxRead`) directly — no task dispatches to itself (`rdm_engine.cpp:96`, `rdm_task.cpp:77-133`).
- **Line selection race condition fix**: `rdmRmtSelect` was moved from core 0 (`handleArtRdm`) to core 1 (RDM task `RDM_CMD_SELECT_LINE` handler, guarded by `cmd.artReqLen && cmd.lineIdx >= 0`) to prevent races when back-to-back ArtNet RDM packets target different outputs (`rdm_task.cpp:78`, `rdm_task.cpp:65-75`). The DE/RE pin is switched atomically with RMT/UART selection inside this core-1 handler.
