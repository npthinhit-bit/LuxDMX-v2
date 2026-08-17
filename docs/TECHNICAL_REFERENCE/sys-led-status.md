# LED Status — Technical Reference

Domain: sys.led-status

## 1. Domain Scope

Owns status-LED hardware abstraction: pin/channel attachment via Arduino `ledcAttachChannel`, RGB colour packing (`0xRRGGBB`) into a single PWM duty value, brightness scaling, and the boot-connecting pulse animation. Supports four LED types via the `ledType` config enum, but the inspected `led_status.cpp` only implements the **plain-GPIO / single-channel PWM** path (one LEDC channel); the 5-LED panel, WS2812, and "off" paths are config-declared but not fully stubbed in the inspected source (see [Open Questions](#18-open-questions)).

Consumers:
- `[sys-tasks](./sys-tasks.md)` — `ledTask` calls `updateLedFromNet()` then `setLedColor(g_ledState.rgb, g_ledState.on)` every 50 ms (`src/sys/tasks.cpp:159-160`); `updateLedFromNet` sets a green (`0x000a00`) "network-active" colour (`src/sys/tasks.cpp:89-90`).
- `src/main.cpp:53-54` — `initLed()` then `setLedColor(0x0a0a0a)` (white) during `setup()`.
- `[sys-crash-guard](./sys-crash-guard.md)` — not directly; the guard logs via `Serial` but does not set the LED.

## 2. Layer Mapping

```
drv  →  cfg  →  core  →  net  →  app/sys
            ↑                            ↑
        ledPin / ledType / ledR..ledW   initLed()
        (config_schema.cpp)            setLedColor()
            |                          ledTask()
            └── src/sys/led_status.cpp
```

Reads config fields from the **cfg** layer (`cfg.ledPin`, `cfg.ledType`, `cfg.ledR/G/Y/B/W`, `cfg.ledBrR..W`) and configures the LEDC PWM hardware via the **drv**-boundary Arduino core (`ledcAttachChannel`, `ledcWriteChannel`) — no ESP-IDF driver header is included directly.

## 3. Source Files

| File | Role |
|---|---|
| `src/sys/led_status.cpp` | `g_ledPin` (line 13), `g_ledCh` (line 14), `g_haveLed` (line 15), `g_ledState` (line 16), `initLed` (line 18), `setLedColor(rgb, on)` (line 32), `setLedColor(rgb)` overload (line 47), `setLedBrightness` (line 49), `bootConnectingLed` (line 55) |
| `src/sys/led_status.h` | `LedState` struct (line 5), `g_ledState` extern (line 10), `initLed`/`setLedColor`/`setLedBrightness`/`bootConnectingLed` declarations (lines 12-16) |
| `src/sys/tasks.cpp` | consumes `g_ledState`, `setLedColor` from `ledTask`; `updateLedFromNet` sets green (`src/sys/tasks.cpp:87-91,159-160`) |
| `include/config_schema.h` | `Config.ledPin` (line 43), `ledType` (line 44), `ledR/G/Y/B/W` (lines 45), `ledBrR/G/Y/B/W` (lines 46) |
| `src/cfg/config_schema.cpp` | `ENUM_LEDTYPE` table (line 12) = `{"off","plain GPIO","WS2812 RGB","5-LED panel"}`, `ledType` field (line 52), `ledPin` (line 51), `ledR..W` (lines 53-57), `ledBrR..W` (lines 58-62) |

## 4. Data Structures

### `LedState` (`src/sys/led_status.h:5-8`)

| Field | Type | Initial | Description |
|---|---|---|---|
| `rgb` | `uint32_t` | `0` | Packed 0xRRGGBB colour; high byte unused. |
| `on` | `bool` | `false` | Whether the LED should emit light. |

### `g_ledState` (`src/sys/led_status.h:10` / `src/sys/led_status.cpp:16`)

`extern LedState g_ledState` — the single live colour state, written by `setLedColor`/`bootConnectingLed` and read by `[sys-tasks](./sys-tasks.md)` `ledTask` (`src/sys/tasks.cpp:160`).

### Static runtime state (`src/sys/led_status.cpp:13-15`)

| Variable | Type | Initial | Description |
|---|---|---|---|
| `g_ledPin` | `uint8_t` | `LED_BUILTIN` (= 2 if undefined, `src/sys/led_status.cpp:5-7`) | GPIO pin for the LEDC channel. |
| `g_ledCh` | `uint8_t` | `0` | LEDC PWM channel 0 (Arduino `ledcAttachChannel` channel). |
| `g_haveLed` | `bool` | `false` | `true` after a successful `ledcAttachChannel`. Gates all PWM writes. |

### `ENUM_LEDTYPE` (`src/cfg/config_schema.cpp:12`)

| Index | Label | Behaviour in inspected source |
|---|---|---|
| 0 | `"off"` | No LED init path observed — `initLed` always calls `ledcAttachChannel` regardless of `ledType`. |
| 1 | `"plain GPIO"` | Implemented: `initLed` attaches LEDC channel (`src/sys/led_status.cpp:22`). |
| 2 | `"WS2812 RGB"` | Not implemented in `led_status.cpp` — `g_ledPin`/LEDC path only drives a single channel. |
| 3 | `"5-LED panel"` | Not implemented in `led_status.cpp` — `cfg.ledR/G/Y/B/W` and `ledBrR..W` are unused by the inspected source. |

## 5. Concurrency

**Single-threaded (core 0, `setup()`), read by a prio-1 task on either core.**

- `initLed()` runs during `setup()` on core 0 (`src/main.cpp:53`), before `createTasks()`.
- `ledTask` (priority 1, unpinned — `src/sys/tasks.cpp:79`) runs `updateLedFromNet()` + `setLedColor()` every 50 ms. It may execute on either core since `xTaskCreate` (no `PinnedToCore`) is used (`src/sys/tasks.cpp:79`).
- `g_ledState` (the shared struct) is written by `setLedColor` (`src/sys/led_status.cpp:33-34`) and by `updateLedFromNet` which writes `g_ledState.rgb`/`g_ledState.on` directly (`src/sys/tasks.cpp:89-90`), then read by `ledTask` (`src/sys/tasks.cpp:160`). The struct fields are plain (non-volatile); the 50 ms task cadence and single-word `uint32_t`/`bool` writes make torn reads non-observable in practice on the ESP32-S3. No locks, seqlocks, or atomics are used.
- `bootConnectingLed` uses a `static uint32_t startMs` (`src/sys/led_status.cpp:57`) — not called from any task in the inspected source (see [Open Questions](#18-open-questions)).

## 6. State Machine

No formal state machine. The LED has two observable output states driven by `g_ledState.on` (`bool`) and `g_ledState.rgb` (`uint32_t`):

- **Booting**: `setup()` calls `setLedColor(0x0a0a0a, true)` → white dim (`src/main.cpp:54`).
- **Network active**: `ledTask` calls `updateLedFromNet()` → green (`0x000a00`), on (`src/sys/tasks.cpp:89-90`).
- **Off**: `setLedColor(any, false)` → `ledcWriteChannel(0)` (duty 0) (`src/sys/led_status.cpp:36-38`).

Brightness is a scalar modifier applied on top of the current colour (`g_ledCh` duty scaled 0–255) — not a separate state.

## 7. Entry Points

| Entry point | Address | Caller |
|---|---|---|
| `initLed()` | `src/sys/led_status.cpp:18` | `setup()` (`src/main.cpp:53`) |
| `setLedColor(uint32_t rgb, bool on)` | `src/sys/led_status.cpp:32` | `main.cpp:54`, `ledTask` (`src/sys/tasks.cpp:160`), `bootConnectingLed` (internal) |
| `setLedColor(uint32_t rgb)` | `src/sys/led_status.cpp:47` | convenience overload; body delegates to `setLedColor(rgb, true)` |
| `setLedBrightness(uint8_t pct)` | `src/sys/led_status.cpp:49` | not called from inspected source (see [Open Questions](#18-open-questions)) |
| `bootConnectingLed()` | `src/sys/led_status.cpp:55` | not called from inspected source (see [Open Questions](#18-open-questions)) |
| `updateLedFromNet()` | `src/sys/tasks.cpp:87` | `ledTask` (`src/sys/tasks.cpp:159`) |

## 8. Data Flow

1. **Pin resolution** — `initLed` reads `cfg.ledPin` (`src/sys/led_status.cpp:19`); if in range (1–48), overrides `g_ledPin` (`src/sys/led_status.cpp:20`). Default is `LED_BUILTIN` (2 if undefined, `src/sys/led_status.cpp:5-7`).
2. **LEDC attach** — `g_haveLed = ledcAttachChannel(g_ledPin, 1000, 8, g_ledCh)` — 1 kHz, 8-bit duty; 1000 Hz × 256 steps (`src/sys/led_status.cpp:22`).
3. **Failure path** — if attach fails, `g_haveLed = false` and a log line is printed; all subsequent `setLedColor`/`setLedBrightness` calls early-return (`src/sys/led_status.cpp:23-28,35,50`).
4. **Colour set** — `setLedColor` stores `rgb`/`on` into `g_ledState` (`src/sys/led_status.cpp:33-34`); if `!on` → duty 0; if `!g_haveLed` → return; else extract R/G/B (`>>16`, `>>8`, `&0xFF`), compute luminance `v = (r+g+b)/3`, write `ledcWriteChannel(g_ledCh, v)` (`src/sys/led_status.cpp:36-44`).
5. **Brightness** — `setLedBrightness(pct)` clamps to 100, then `ledcWriteChannel(g_ledCh, map(pct, 0, 100, 0, 255))` (`src/sys/led_status.cpp:49-53`) — overwrites whatever colour was set.
6. **Task-driven update** — `ledTask` calls `updateLedFromNet()` (sets green `0x000a00`) then `setLedColor(g_ledState.rgb, g_ledState.on)` every 50 ms (`src/sys/tasks.cpp:159-161`).

## 9. Protocol Layout

N/A (no wire protocol). The LED is a local indicator only. The colour is packed as a 32-bit integer `0x00RRGGBB` (`src/sys/led_status.h:6`).

## 10. Config Integration

| Field | CFG flag | Schema line (`config_schema.cpp`) | Read in (`led_status.cpp`) |
|---|---|---|---|
| `ledPin` | `CFG_REBOOT` | line 51 (`IFIELD(... -1, 48 ...)`) | `initLed` (line 19) |
| `ledType` | `CFG_REBOOT` | line 52 (`EFIELD(..., ENUM_LEDTYPE)`) | not read by inspected source |
| `ledR` | `CFG_REBOOT` | line 53 | not read |
| `ledG` | `CFG_REBOOT` | line 54 | not read |
| `ledY` | `CFG_REBOOT` | line 55 | not read |
| `ledB` | `CFG_REBOOT` | line 56 | not read |
| `ledW` | `CFG_REBOOT` | line 57 | not read |
| `ledBrR` | `CFG_LIVE` | line 58 (`IFIELD_L`) | not read |
| `ledBrG` | `CFG_LIVE` | line 59 | not read |
| `ledBrY` | `CFG_LIVE` | line 60 | not read |
| `ledBrB` | `CFG_LIVE` | line 61 | not read |
| `ledBrW` | `CFG_LIVE` | line 62 | not read |

Only `ledPin` is consumed by the inspected `led_status.cpp`. The `ledType`-driven multi-LED / WS2812 paths are declared in config but not implemented in the inspected source — see [Open Questions](#18-open-questions). Writes: none.

## 11. Lifecycle

- **Init (core 0, `setup()`):** `initLed()` — resolves pin, attaches LEDC channel, sets `g_ledState.on = true` (`src/sys/led_status.cpp:18-30`); then `setLedColor(0x0a0a0a, true)` (white, booting) (`src/main.cpp:54`).
- **Runtime (prio-1, 50 ms):** `ledTask` calls `updateLedFromNet()` → `setLedColor(g_ledState.rgb, g_ledState.on)` (`src/sys/tasks.cpp:159-161`).
- **Shutdown:** None — the LEDC channel is never detached; on reboot the channel is re-initialized.

## 12. Error Handling

- `initLed`: if `ledcAttachChannel` fails, `g_haveLed = false` and a log line prints (`src/sys/led_status.cpp:23-28`); no `esp_err_t` is propagated — failure is degraded (LED off), not fatal.
- `setLedColor`: early-returns if `!g_haveLed` (`src/sys/led_status.cpp:35`); otherwise always writes `g_ledState` first, so the struct is updated even when no hardware is attached (useful for the WebSocket status path).
- `setLedBrightness`: early-returns if `!g_haveLed` (`src/sys/led_status.cpp:50`); clamps `pct > 100` to 100 (`src/sys/led_status.cpp:51`).
- `bootConnectingLed`: early-returns if `!g_haveLed` (`src/sys/led_status.cpp:56`).
- No exceptions / `noexcept` — all `void` functions; `ledcAttachChannel`/`ledcWriteChannel` return `bool` (Arduino ESP32) but the return is only checked in `initLed` (`src/sys/led_status.cpp:22`).

## 13. Memory Allocation

- `g_ledPin`, `g_ledCh`, `g_haveLed`, `g_ledState` — static file-scope in `.bss`/`.data` (`src/sys/led_status.cpp:13-16`).
- `startMs` (in `bootConnectingLed`) — `static uint32_t` (`src/sys/led_status.cpp:57`).
- No heap allocation. LEDC channel config is held by the Arduino/ESP-IDF LEDC driver, not by this module.

## 14. Timing

| Item | Value | Source |
|---|---|---|
| LEDC PWM frequency | 1000 Hz | `src/sys/led_status.cpp:22` (`ledcAttachChannel(..., 1000, 8, ...)`) |
| LEDC duty resolution | 8-bit (256 steps) | `src/sys/led_status.cpp:22` |
| `ledTask` period | 50 ms | `src/sys/led_status.cpp:161` (in `src/sys/tasks.cpp:79,161`) |
| `bootConnectingLed` phase | 400 ms cycle (200 on / 200 off) | `src/sys/led_status.cpp:58-61` |
| `initLed` | one-shot, sub-millisecond | `src/sys/led_status.cpp:18-30` |

The 50 ms `ledTask` period is the only hard cadence; all colour writes are sub-microsecond.

## 15. Traceability

| Claim | Evidence |
|---|---|
| `LED_BUILTIN` defaults to 2 if undefined | `src/sys/led_status.cpp:5-7` |
| Runtime state: `g_ledPin`, `g_ledCh`, `g_haveLed`, `g_ledState` | `src/sys/led_status.cpp:13-16` |
| `initLed` resolves `cfg.ledPin` in range 1–48 | `src/sys/led_status.cpp:19-21` |
| LEDC attach: 1 kHz, 8-bit, channel 0 | `src/sys/led_status.cpp:22` |
| Attach failure sets `g_haveLed = false` + log | `src/sys/led_status.cpp:23-28` |
| `g_ledState.on = true` after init | `src/sys/led_status.cpp:29` |
| `setLedColor` stores rgb+on, computes luminance `(r+g+b)/3` | `src/sys/led_status.cpp:32-44` |
| `setLedColor` early-returns if `!g_haveLed` | `src/sys/led_status.cpp:35` |
| `setLedColor(rgb)` delegates with `on=true` | `src/sys/led_status.cpp:47` |
| `setLedBrightness` clamps to 100, maps 0–100→0–255 | `src/sys/led_status.cpp:49-53` |
| `bootConnectingLed` 400 ms phase, 50 max luminance | `src/sys/led_status.cpp:55-62` |
| `updateLedFromNet` sets green `0x000a00` | `src/sys/tasks.cpp:89` |
| `ledTask` cadence 50 ms, calls `setLedColor(g_ledState...)` | `src/sys/tasks.cpp:159-161` |
| `initLed` called from `setup()` | `src/main.cpp:53` |
| `setLedColor(0x0a0a0a)` booting white from `setup()` | `src/main.cpp:54` |
| `ENUM_LEDTYPE` = off/plain GPIO/WS2812/5-LED panel | `src/cfg/config_schema.cpp:12` |
| `ledPin` schema: `IFIELD`, CFG_REBOOT | `src/cfg/config_schema.cpp:51` |

## 16. Cross-References

- `[sys-tasks](./sys-tasks.md)` — `ledTask` (prio 1, 50 ms) is the LED consumer (`src/sys/tasks.cpp:159-161`); `updateLedFromNet` sets the green network-active colour (`src/sys/tasks.cpp:87-90`).
- `config-engine` — `cfgcore::load()` populates `cfg.ledPin`/`ledType`/`ledR..W`/`ledBrR..W` before `initLed` (`src/main.cpp:46,53`).
- `[sys-crash-guard](./sys-crash-guard.md)` — the guard does not set the LED, but boot-phase log lines (`Serial.printf`) accompany guard progress (`src/sys/tasks.cpp:49,74`); no direct call dependency.

## 17. Limitations

- Only the **single-channel PWM** (plain GPIO) LED path is implemented. `ledType` values 0 (off), 2 (WS2812), and 3 (5-LED panel) are declared in config but `initLed` unconditionally calls `ledcAttachChannel` — there is no `switch` on `cfg.ledType` in the inspected source.
- The 5-LED panel pins (`cfg.ledR/G/Y/B/W`, `ledBrR/G/Y/B/W`) and per-colour brightness are **completely unused** by `led_status.cpp`; a WS2812 or 5-LED panel board will only drive a single LED.
- `g_ledState` is written by `updateLedFromNet` (`src/sys/tasks.cpp:89-90`) and `setLedColor` (`src/sys/led_status.cpp:33-34`) and read by `ledTask` with no `volatile` qualifier on the struct — relies on the 50 ms cadence and same-core (typically) access to avoid torn reads.
- `setLedBrightness` overwrites the colour duty with a brightness-only value — calling it after `setLedColor` loses the colour (luminance is replaced by the mapped scalar).
- Luminance uses a naive `(r+g+b)/3` average (`src/sys/led_status.cpp:43`) rather than a perceptually-weighted formula; pure blue (0x0000aa) will appear dimmer than pure red at the same raw value.

## 18. Open Questions

1. Not determinable from the inspected source code — where/when `bootConnectingLed()` (`src/sys/led_status.cpp:55`) is called; no caller was found in `tasks.cpp`, `main.cpp`, or the net-state module. Likely during WiFi connection wait.
2. Not determinable from the inspected source code — where `setLedBrightness` is called; no caller in the inspected source.
3. Not determinable from the inspected source code — the WS2812 (`ledType` 2) and 5-LED panel (`ledType` 3) rendering paths; the multi-pin/brightness config fields exist in `Config` (`include/config_schema.h:44-46`) but are not read by `led_status.cpp`. Either a different source file (not inspected) implements these, or they are unimplemented stubs.
4. Not determinable from the inspected source code — whether the `LedState` struct is also consumed by a WebSocket status payload (the WS frame builder is `src/net/ws_frame.cpp`, not inspected for LED coupling).

## 19. Testing

- No host-native test covers `led_status.cpp` — `ledcAttachChannel`/`ledcWriteChannel` require the ESP32 Arduino HAL; the `test/native/shim/` shims do not provide them.
- `config_test.cpp` asserts `cfg.ledType == 3` ("5-LED panel") as a default-template resolution check (`test/native/config_test.cpp:24,54`) — this tests the **config engine**, not the LED driver.
- `setLedColor`'s RGB→luminance extraction and `setLedBrightness`'s `map(0..100 → 0..255)` are pure functions that could be extracted and unit-tested but are not in the inspected test files.
- The boot-connecting animation (400 ms phase) and the green network-active colour are validated visually during the 5-minute firmware evaluation workflow (`CLAUDE.md`).

## 20. History

- `LedState` struct extracted into `led_status.h` during the 5-layer refactor to decouple LED state from `main.cpp`; `g_ledState` is now the canonical colour store consumed by `[sys-tasks](./sys-tasks.md)` `ledTask`.
- `updateLedFromNet` added to `tasks.cpp` (`src/sys/tasks.cpp:87-91`) to set the green "network active" indicator from the netRxTask side without crossing core boundaries.
- `ledcAttachChannel` API used (Arduino-esp32 v3) replaces the older `ledcSetup`/`ledcAttachPin` pair — the comment at `src/sys/led_status.cpp:4` notes the v3 LED_BUILTIN omission.
- `ledType` enum (`ENUM_LEDTYPE`, `src/cfg/config_schema.cpp:12`) was extended to include "5-LED panel" (index 3) for the LuxDMX v6 PCB, but the driver implementation lags behind config support.
