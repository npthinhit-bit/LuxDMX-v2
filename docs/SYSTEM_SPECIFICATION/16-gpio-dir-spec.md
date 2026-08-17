# GPIO DE/RE Direction Control Specification

Domain: drv.gpio-dir

## 1. Module Overview

The GPIO DE/RE direction control module controls the DE (Driver Enable) and RE (Receiver Enable) pin of a half-duplex RS485 transceiver used for DMX/RDM. A single GPIO is wired to both DE and RE (tied together on typical DMX/RDM transceiver modules): HIGH = drive (TX enable), LOW = listen (RX enable). The module is a two-function header-only driver with no internal state and no dependencies beyond the ESP-IDF GPIO driver.

It is consumed exclusively by the RDM engine on core 1, which toggles DE/RE around RDM request transmission and response listening. The DMX TX path (RMT) runs uninterrupted — the DE/RE GPIO is never switched back to RX during normal DMX output, so DMX framing is never disturbed by RDM bus turnaround. This was the fix for issue #64's core requirement that DMX output remain contention-proof under RDM operations.

The module owns nothing else. It does not initialize the RMT channel, configure the UART, or assemble frames — it only sets a GPIO level at the boundaries of an RDM transaction.

## 2. External Interfaces

### Caller-Facing API

| Function | Signature | Behavior |
|---|---|---|
| `gpioDeInit` | `(int pin)` | Configures the pin as a GPIO output and sets it to the idle-high (drive) level. Guards against `pin < 0` (no-op). Returns `void`. |
| `gpioDeSet` | `(int pin, int level)` | Sets the DE/RE GPIO to the given level. Returns `void`. Does not guard against `pin < 0`. |

### Data Structures

This module defines no data structures. It operates directly on integer GPIO pin numbers and the ESP-IDF GPIO driver. All state it influences is held in caller-owned structs:

| Caller struct | Field | What it holds |
|---|---|---|
| `RdmLine` | `de` | DE/RE GPIO pin number for this RDM line |
| `dmx_output_t` | `dePin` | DE/RE pin, copied from config `rtsPin` |

### Electrical Interface

| GPIO level | DE | RE | Transceiver state | Direction |
|---|---|---|---|---|
| HIGH (1) | High | High | Driver enabled, receiver disabled | TX (drive) |
| LOW (0) | Low | Low | Driver disabled, receiver enabled | RX (listen) |

This matches the standard RS485 DE/RE pinout where both are active-high and tied together for half-duplex operation.

## 3. State Machine

No state machine — stateless request/response. `gpioDeInit` is called once during initialization to configure the pin as output and set the idle level. `gpioDeSet` is called repeatedly during RDM transactions to toggle between drive (TX) and listen (RX). The current pin level lives in the GPIO hardware register, not in this module. There are no entry/exit events beyond the write itself.

The effective state transitions occur in the RDM transaction lifecycle:
- After `gpioDeInit`: pin = HIGH (idle drive, DMX output active)
- Before RDM TX: pin set HIGH (drive)
- After RDM TX, before response: pin set LOW (listen), 90 us settling delay
- After response read: pin set HIGH (drive, DMX output resumes)

## 4. Data Flow

### RDM Transaction DE/RE Toggle Sequence

1. **Init:** `gpioDeInit(dePin)` is called during output initialization for each RDM-capable output. The pin is configured as output and set to HIGH (drive) so that DMX output can continue uninterrupted.

2. **RDM request transmit:** `rdmTx()` sets DE/RE HIGH (drive), encodes and transmits the RDM request over RMT, waits for RMT completion, then:
   - Sets DE/RE LOW (listen)
   - Delays 90 us for transceiver turn-round
   - Flushes the UART RX FIFO to discard stale bytes

3. **RDM response receive:** The RDM engine reads the response on the dedicated RX UART within a 9 ms window. The DE/RE pin stays LOW (listen) during this entire window.

4. **Return to DMX:** After the response read completes, DE/RE is set HIGH (drive) so the 1 ms `dmxTxTask` loop can resume DMX frame transmission. The DE/RE pin stays HIGH during all subsequent DMX TX ticks until the next RDM transaction.

### Pin Resolution Path

The DE/RE pin originates in config as `rtsPin` (reboots-required field). The mode resolver returns RDM-full mode if `mode == OUTPUT_MODE_RDM_FULL` or `rtsPin >= 0`. The output init code passes the resolved pin to the RDM line initializer, which stores it and calls `gpioDeInit`.

## 5. Configuration Integration

This module reads no configuration fields directly. The DE/RE pin originates in config and is resolved by its callers:

| Config field | Resolution path | CFG flag | Live/Reboot |
|---|---|---|---|
| `rtsPin` | Config -> `resolveOutputMode()` (RDM-full if >= 0) -> output init -> RDM init -> stored in pin field | CFG_REBOOT | Reboot |
| `mode` | Config -> `resolveOutputMode()` | CFG_REBOOT | Reboot |
| `port` | Config -> RDM line UART mapping | CFG_REBOOT | Reboot |

No live config fields affect DE/RE behavior — the pin and its mode are fixed at initialization. The DE/RE pin stays constant for the lifetime of the device unless rebooted.

## 6. Lifecycle

| Phase | Function | Notes |
|---|---|---|
| Init | `gpioDeInit(pin)` | Called during output initialization for each RDM-capable output. Configures GPIO as output, sets idle HIGH (drive). One-shot. |
| Runtime | `gpioDeSet(pin, level)` | Called from RDM transaction path on core 1: HIGH (drive) before/ TX, LOW (listen) during response window, HIGH (drive) after response read. |
| Cleanup | None | No deinit or teardown. The GPIO stays as output until reset. The RMT channel is also not released, by design. |

The init sequence wraps output initialization with the crash guard's NVS-backed counter, which may disable outputs before this module runs.

## 7. Error Handling

| Function | Return | Failure condition | Behavior |
|---|---|---|---|
| `gpioDeInit` | `void` | `pin < 0` | Returns early (no-op). Pin validity assumed. |
| `gpioDeInit` | `void` | `gpio_config` failure | Return value **not checked**; silently ignored |
| `gpioDeSet` | `void` | `pin < 0` | **No guard**; calls `gpio_set_level` unconditionally |
| `gpioDeSet` | `void` | Invalid pin / hardware error | ESP-IDF returns error silently (no logging) |

No `ESP_LOGE` or `Serial.println` calls exist in this module. Error visibility depends entirely on the caller propagating or logging upstream failures, which the RDM transport layer does not do — GPIO-level errors are not surfaced to higher layers.

**Known inconsistency:** `gpioDeInit` guards against `pin < 0` but `gpioDeSet` does not. If called with a negative or invalid pin, `gpio_set_level` will return an error that is silently dropped.

## 8. Timing Constraints

| Constraint | Value | Basis |
|---|---|---|
| GPIO set level latency | < 1 us | ESP32 GPIO matrix |
| DE/RE drive-to-listen settling | 90 us | Transceiver turn-round delay (added by RDM engine) |
| RDM TX to RX turnaround | 90 us + response window | E1.20 controller timing: listen-after-talk delay |
| DE/RE listen-to-drive | Immediate | GPIO write at response-read completion |
| DMX uninterrupted window | From drive-set to next RDM transaction | DE/RE stays HIGH during all 1 ms DMX ticks between RDM ops |
| `dmxTxTask` tick | 1 ms | Core 1, priority 19 — DMX frames sent every 1 ms regardless of RDM state |

The DE/RE toggle timing is governed by the RDM engine, not by this module. This module only asserts that the level change is immediate (raw GPIO write with no delay).

## 9. Memory & Allocation Model

**None.** This module performs no heap (`malloc`/`heap_caps_malloc`) or stack allocation beyond a 16-byte `gpio_config_t` struct on the caller's stack (zero-initialized). No PSRAM is involved. The `pin_bit_mask` field is set via bit-shift on the pin number.

## 10. Safety Considerations

- **Core isolation:** Every call to `gpioDeSet` and `gpioDeInit` originates from RDM operations on core 1 (RDM task priority 18, or output init at priority 19). Core 0 (network/WiFi) never touches these GPIO pins for DE/RE control. Since only one core writes the pin and only during RDM transactions (never from the 1 ms `dmxTxTask` frame loop), no lock or atomic operation is needed.
- **No DMX interruption:** The `dmxTxTask` 1 ms loop calls only `rmtDmxKick` for DMX output — it never calls `gpioDeSet`. The DE/RE pin stays HIGH (drive) from the most recent `gpioDeSet(de, 1)` call throughout all DMX TX ticks between RDM transactions. DMX output is never disturbed by RDM listen windows.
- **Idle-high default:** `gpioDeInit` sets the pin to HIGH (drive) on initialization. This ensures that when DMX output begins, the transceiver is always in drive mode. The pin is only switched to LOW (listen) during the brief RDM response window.
- **Turn-round delay:** The 90 us delay after switching to listen ensures the RS485 transceiver has fully transitioned to receive mode before the UART RX is armed, preventing missed or corrupted response bytes.
- **No state readback:** There is no mechanism to query the current DE/RE level. Correctness relies entirely on the RDM engine's call ordering (set drive before TX, set listen after TX, set drive after response).

## 11. Cross-Module Dependencies

| Module | Provides to this module | Consumes from this module |
|---|---|---|
| DMX RMT TX | Shared RMT TX channel for RDM requests | — |
| DMX UART RX | UART RX primitive for RDM responses | — |
| RDM Engine | Calls `gpioDeInit` (init), `gpioDeSet` (transaction boundaries) | DE/RE GPIO control |
| RDM Discovery | Calls `gpioDeSet` (post-branch listen) | — |
| RDM Task | Calls `gpioDeSet` (raw relay post-response) | — |
| Output Init | Config fields (`rtsPin`, `mode`, `port`) -> RDM line init | — |
| Sys Tasks | `dmxTxTask` (core 1, priority 19) drives the RMT that DE/RE gates | — |

This module depends only on the ESP-IDF GPIO driver API. It has no dependencies on the config, core, net, or app/sys layers.

## 12. Testing Verification

No host-native or hardware test coverage exists for this module. No native or unit test files reference `gpioDeInit`, `gpioDeSet`, `dmx_output_t.dePin`, or `RdmLine.de`. This module is a pure hardware GPIO wrapper with no shim-compatible test path — GPIO configuration requires real ESP-IDF hardware context. The `rdm_types_test` does not cover GPIO.

## 13. Open Questions

- Whether `gpioDeSet` is ever called with a pin that was not first `gpioDeInit`-ed; the function has no internal guard, so a caller error would silently write to an unconfigured GPIO.
- Whether the 90 us turn-round delay is empirically validated against the specific RS485 transceiver used in the LuxDMX hardware, or whether it was copied from library defaults.
- Whether any board variant uses a transceiver with active-low DE/RE, which would require this module to invert its logic (currently hard-coded as active-high).
- Whether the `pin < 0` guard gap in `gpioDeSet` (absent) versus `gpioDeInit` (present) was intentional (callers always pre-validate) or an oversight.
- Whether separate DE/RE pins (not tied together) are planned, which would require splitting the init/set functions into per-signal controls.

## 14. History

- **Issue #64 (core separation):** The DE/RE GPIO was introduced so that RDM could toggle transceiver direction without releasing the RMT TX channel. The design constraint is documented: the RMT TX channel is never released — DMX output on this pin is uninterrupted between RDM transactions. This prevents the original issue-64 corruption where core-0 network DMA delayed break timing.
- **RDM architecture decision:** The RDM engine documents its transport as "RDM requests go out over RMT (same channel as DMX), responses come back on a RX-only UART, DE/RE is a GPIO." This module implements exactly the DE/RE half of that triad. The architectural decision record states that the RDM engine uses low-level primitives directly — no task dispatches to itself.
- **Line selection race condition fix:** `rdmRmtSelect` was moved from core 0 to core 1 to prevent races when back-to-back ArtNet RDM packets target different outputs. The DE/RE pin is switched atomically with RMT/UART selection inside this core-1 handler.
