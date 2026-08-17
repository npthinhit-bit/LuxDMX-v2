# Quadrature Encoder Decode — Technical Reference

Domain: app.enc-decode

## 1. Domain Scope

This module provides the **lowest-level mechanical decode** of a rotary encoder's A/B quadrature signals and an optional push-button. It is a single header file (`src/app/enc_decode.h`) containing pure functions and plain-old-data structs with no dependencies beyond `<stdint.h>`.

It owns:
- Quadrature state-table decoding (`encStep`).
- Per-detent accumulation with configurable steps-per-detent and direction reversal (`EncDetent`).
- Push-button edge debouncing + short/long press classification (`EncButton`).

It delegates:
- Nothing. There is no UART, GPIO, or timer interaction here. The caller (firmware poll loop in `main.cpp` or the test harness) reads raw A/B and button levels and feeds them in with a millisecond timestamp.

It is consumed by:
- **`input_map.h`** (`src/app/input_map.h:31`) — includes this header, instantiates `EncDetent` and `EncButton`, and turns raw motions/presses into abstract `NavEvent`s. The layer below input mapping is documented at `src/app/enc_decode.h:10-13`.

This module does **not** interpret what a step or press *means*; it only reports raw motion (+1/0/-1) and raw press classification (SHORT/LONG/NONE). Semantics are decided one layer up in `input_map.h`.

## 2. Layer Mapping

| Layer | Module path | Role here |
|---|---|---|
| **app** | `src/app/enc_decode.h` | Bottom of the application input stack: raw quadrature + button decode primitives |
| **app** | `src/app/input_map.h` | Consumes `EncDetent`, `EncButton`, `EncPress`, `encStep`, `ENC_LONG_PRESS_MS` |
| **cfg** | `src/cfg/config_schema.cpp` | `encSteps` (line 75) → `EncDetent.perDetent`; `encReverse` (line 76) → `EncDetent.reverse`; `btnActiveHigh` (line 85) → `InputConfig.activeHigh`; long-press threshold via `ENC_LONG_PRESS_MS` |
| **include** | `include/config_schema.h` | `Config.encSteps` (line 51), `Config.encReverse` (line 52) — persisted settings consumed by `InputConfig` |

## 3. Source Files

| File | Role |
|---|---|
| `src/app/enc_decode.h` | Entire module — 140-line header-only file. No `.cpp`. Contains `encStep()`, `EncDetent`, `EncPress`, `ENC_DEBOUNCE_MS`, `ENC_LONG_PRESS_MS`, `encClassifyPress()`, `EncButton`. |
| `src/app/input_map.h` | Consumer — includes `enc_decode.h` (line 31), instantiates `EncDetent dec` and `EncButton sw/btn[4]` in `InputMapper` (lines 108-110). |
| `src/cfg/config_schema.cpp` | Provides config fields that initialize the decode structs: `encSteps` (line 75), `encReverse` (line 76). |

No `.cpp` file exists for this module. All functions are `static inline` or struct methods, compiled directly into the translation unit that includes the header (currently only `input_map.h`, which is itself only included by `menu.h` — neither of which is yet wired into `main.cpp`).

## 4. Data Structures

### `EncDetent` — `src/app/enc_decode.h:58-82`

Per-detent quadrature accumulator. Tracks previous A/B reading and sums `encStep()` results until a full mechanical detent is reached.

| Field | Type | Line | Description |
|---|---|---|---|
| `prevAB` | `uint8_t` | 59 | Previous 2-bit A/B reading (`0xFF` = uninitialized; first `feed()` call just seeds) |
| `accum` | `int16_t` | 60 | Running sum of `encStep()` deltas; wraps around `perDetent` |
| `perDetent` | `uint8_t` | 61 | Quadrature edges per emitted step (1/2/4). Default 4 = standard EC11 detented encoder |
| `reverse` | `bool` | 62 | If true, flips the sign of each step (swaps CW/CCW) |

Constructor at line 65: `EncDetent(uint8_t transitionsPerDetent, bool rev = false)` — guards against `perDetent == 0` by substituting 1 (line 66).

Method:
- `feed(uint8_t a, uint8_t b)` (line 69) — returns `+1`, `-1`, or `0` per completed detent.

### `EncPress` — `src/app/enc_decode.h:89-93`

| Enumerator | Value | Line | Description |
|---|---|---|---|
| `ENC_PRESS_NONE` | 0 | 90 | No completed press |
| `ENC_PRESS_SHORT` | 1 | 91 | Press shorter than long threshold, longer than debounce floor |
| `ENC_PRESS_LONG` | 2 | 92 | Press held ≥ long threshold |

### Constants — `src/app/enc_decode.h:95-96`

| Constant | Value | Line | Description |
|---|---|---|---|
| `ENC_DEBOUNCE_MS` | 25 | 95 | Sub-25 ms blips are noise; ignore |
| `ENC_LONG_PRESS_MS` | 600 | 96 | ≥600 ms held = long press |

### `EncButton` — `src/app/enc_decode.h:108-138`

Edge-debounced button tracker. Tracks raw level changes, debounces at `ENC_DEBOUNCE_MS`, and classifies completed presses by hold duration.

| Field | Type | Line | Description |
|---|---|---|---|
| `stable` | `bool` | 109 | Last debounced logical state (pressed?) |
| `lastRaw` | `bool` | 110 | Last raw level seen (for edge detection) |
| `lastChangeMs` | `uint32_t` | 111 | Timestamp of last raw level change |
| `pressStartMs` | `uint32_t` | 112 | Timestamp of debounced press edge |
| `holdMs` | `uint32_t` | 113 | Long-press threshold (default `ENC_LONG_PRESS_MS`) |

Constructor at line 116: `explicit EncButton(uint32_t longPressMs)` — guards against 0 by falling back to `ENC_LONG_PRESS_MS`.

Methods:
- `feed(bool pressedNow, uint32_t nowMs, uint32_t* heldOut = nullptr)` (line 121) — returns `EncPress` on release edge, `ENC_PRESS_NONE` otherwise. Optional `heldOut` receives current press duration for "keep holding" UI hints.
- `isHolding(uint32_t nowMs)` (line 135) — returns true once a still-held press has crossed the long threshold.

## 5. Concurrency

**Single-threaded by design.** This module contains no synchronization primitives, no locks, no atomics. It is designed to be called from a single polling context — either:

| Caller | Core | Period | Line |
|---|---|---|---|
| Firmware poll loop | core 0 (`loop()`) | ~1 kHz (1 ms) | Not yet wired into `main.cpp` |
| Host test `main()` | N/A (native) | N/A | Each `*_test.cpp` |

The `EncDetent` and `EncButton` structs hold mutable state (`prevAB`, `accum`, `stable`, `pressStartMs`, etc.) that is not protected against concurrent access. The design intent (`src/app/enc_decode.h:5-8`) is that the caller samples raw GPIO levels in a single task and feeds them to the struct synchronously. The ESP32 hardware input poller would run on core 0 alongside the web server, but since encoder sampling is sub-kHz and stateless between samples, no seqlock or SPSC ring is needed.

No FreeRTOS primitives are used. The `millis()` calls are the caller's responsibility (the caller passes `nowMs` to `feed()`).

## 6. State Machine

Two independent stateful trackers:

### EncDetent state machine (`src/app/enc_decode.h:58-82`)

| State | Description |
|---|---|
| **Seeded** (`prevAB == 0xFF`) | No sample seen yet; `feed()` seeds `prevAB` and returns 0 (line 71). |
| **Tracking** (`prevAB != 0xFF`) | Accumulating `encStep()` deltas into `accum`. Emits `+1` when `accum >= perDetent` (line 78), `-1` when `accum <= -perDetent` (line 79), subtracting/adding `perDetent` to keep the fractional remainder. Returns 0 between detents. |
| **No edge** | When `cur == prevAB` (line 72) — no transition, returns 0 immediately. |

No explicit enumerated state variable; the state is implicit in `prevAB` and `accum`.

### EncButton state machine (`src/app/enc_decode.h:108-138`)

| State | Entry condition | Output |
|---|---|---|
| **Idle** | `stable == false` | No press in progress; `feed()` returns `ENC_PRESS_NONE` |
| **Debounce window** | `pressedNow != lastRaw` and `nowMs - lastChangeMs < ENC_DEBOUNCE_MS` (line 124) | No state change; waits for signal to settle |
| **Pressed** (`stable == true`) | Debounced press edge at line 126: `pressStartMs = nowMs` | Returns `ENC_PRESS_NONE`; `heldOut` reports duration |
| **Released** (`stable == false`) | Debounced release edge at line 127: calls `encClassifyPress(nowMs - pressStartMs, holdMs)` (line 127) | Returns `ENC_PRESS_SHORT` or `ENC_PRESS_LONG` |

## 7. Entry Points

This header-only module has no scheduler-called entry points. All functions are called by the consumer (`input_map.h`):

| Entry point | First call site | Line | Caller |
|---|---|---|---|
| `encStep(prevAB, curAB)` | `EncDetent::feed()` | `src/app/enc_decode.h:73` | Called once per `feed()` to compute the delta for the A/B transition |
| `EncDetent::feed(a, b)` | `InputMapper::poll()` | `src/app/input_map.h:148` | Called once per poll cycle if encoder is present and sampled |
| `encClassifyPress(heldMs, holdMs)` | `EncButton::feed()` | `src/app/enc_decode.h:127` | Called on the release edge to classify press length |
| `EncButton::feed(pressedNow, nowMs, heldOut)` | `InputMapper::poll()` | `src/app/input_map.h:154` (sw), `src/app/input_map.h:166` (btns) | Called once per poll per physical button |
| `EncButton::isHolding(nowMs)` | Not called in inspected sources | — | Available for "keep holding" UI hints but no caller currently |
| `EncDetent::EncDetent(transitionsPerDetent, rev)` | `InputMapper::begin()` | `src/app/input_map.h:119` | Constructs the accumulator from config |
| `EncButton::EncButton(longPressMs)` | `InputMapper::begin()` | `src/app/input_map.h:120-121` | Constructs the debounce trackers from config |

## 8. Data Flow

1. **Firmware poll** (`main.cpp` loop — not yet wired) reads raw GPIO levels from encoder A/B pins and button pins. Currently not connected.
2. **Input sample construction**: caller builds `InputSample` (defined in `input_map.h:57-64`) with `encPresent`, `a`, `b`, `swPresent`, `swLevel`, `btnPresent[4]`, `btnLevel[4]`.
3. **`InputMapper::poll(s, now)`** (`input_map.h:145`):
   - If encoder present: calls `dec.feed(s.a, s.b)` (line 148) → `EncDetent::feed` → `encStep(prevAB, cur)` (line 73). If step > 0, pushes `NAV_INC`; if < 0, pushes `NAV_DEC` (lines 149-150).
   - If encoder switch present: calls `sw.feed(pressed(swLevel), now)` (line 154) → on release edge, calls `encClassifyPress` (line 127). Maps SHORT→ENTER, LONG→BACK (or solo-mode INC→ENTER, lines 155-161).
   - For each separate button: calls `btn[i].feed(pressed(btnLevel[i]), now)` (line 166) → on release, classifies and maps to NAV event via `mapRole` (line 172).
4. **Event dequeue**: caller calls `InputMapper::next()` (line 176) → `q.pop()` (line 176 → `NavQueue::pop` at `input_map.h:94`) to drain queued `NavEvent`s.
5. **Menu consumption**: each `NavEvent` is passed to `Menu::handle(NavEvent)` (line 116 of `menu.h`).

## 9. Protocol Layout

N/A (no wire protocol). This module decodes physical GPIO signal transitions (quadrature A/B levels and button press/release edges) into semantic step and press-classification values. There is no network protocol, packet format, or byte stream involved.

## 10. Config Integration

This module reads no `Config` fields directly — it is passed values by `InputConfig` (defined in `input_map.h:67-80`). The config-to-struct wiring happens in `InputMapper::begin()` at `src/app/input_map.h:117-130`:

| Config field | Schema line | CFG flags | Wired to (input_map.h line) |
|---|---|---|---|
| `encSteps` | `config_schema.cpp:75` | `CFG_LIVE` (`IFIELD_L`) | `EncDetent::perDetent` → `InputConfig.encSteps` (line 70) → `dec = EncDetent(c.encSteps, ...)` (line 119) |
| `encReverse` | `config_schema.cpp:76` | `CFG_LIVE` (`BFIELD_L`) | `EncDetent::reverse` → `InputConfig.encReverse` (line 71) → `EncDetent(c.encReverse)` (line 119) |
| `btnActiveHigh` | `config_schema.cpp:85` | `CFG_LIVE` (`BFIELD_L`) | `InputConfig.activeHigh` (line 78) → `pressed()` polarity flip (line 143) |
| Long-press threshold | `ENC_LONG_PRESS_MS = 600` (`enc_decode.h:96`) | N/A (constexpr) | `InputConfig.longMs` (line 79) → `EncButton::holdMs` (lines 120, 121) |

The `cfg.outputs[i].encA/B/Sw` pin assignments (`config_schema.cpp:72-74`, `CFG_REBOOT`) are hardware pin numbers read by the firmware poll loop — not by this decode module. This module receives already-sampled logical levels.

## 11. Lifecycle

| Phase | Function | Call site | Notes |
|---|---|---|---|
| Construct | `EncDetent(transitionsPerDetent, rev)` | `InputMapper::begin` (`input_map.h:119`) | Initializes `prevAB = 0xFF`, `accum = 0`, `perDetent`, `reverse` |
| Construct | `EncButton(longPressMs)` | `InputMapper::begin` (`input_map.h:120-121`) | Initializes debounced tracking state |
| Poll | `EncDetent::feed(a, b)` | `InputMapper::poll` (`input_map.h:148`) | Called every poll cycle; seeds on first call, then decodes transitions |
| Poll | `EncButton::feed(pressedNow, nowMs)` | `InputMapper::poll` (`input_map.h:154,166`) | Debounces + classifies on release edge |
| Destroy | (none) | — | Structs are stack/RAM-resident; no teardown needed |

No init/deinit, no FreeRTOS task, no timer callback. The module is purely pull-based: the caller drives all timing.

## 12. Error Handling

| Return | Line | Behavior |
|---|---|---|
| `int8_t` from `encStep` | `src/app/enc_decode.h:45` | Returns 0 for impossible transitions (double-bit bounce, lines 40-43 TBL). No error path; bounce is silently classified as no-motion. |
| `int8_t` from `EncDetent::feed` | `src/app/enc_decode.h:69-81` | Returns `+1`/`-1`/`0`. No error return; invalid `perDetent` is clamped to 1 in constructor (line 66). |
| `EncPress` from `encClassifyPress` | `src/app/enc_decode.h:100-103` | Returns `ENC_PRESS_NONE` for sub-debounce durations (< 25 ms, line 101). No other error path. |
| `EncPress` from `EncButton::feed` | `src/app/enc_decode.h:121-131` | Returns `ENC_PRESS_NONE` when no event. `ENC_PRESS_NONE` is returned on noise (below debounce floor). |
| `bool` from `EncButton::isHolding` | `src/app/enc_decode.h:135-137` | Returns false if not pressed or below threshold. |
| `void` push to full queue | `input_map.h:88-93` | `NavQueue::push` silently drops if queue is full (all 8 slots occupied); caller cannot detect the drop. |

No `ESP_LOGE`, no `Serial.printf`, no `esp_err_t` — this module is silent by design. All error detection is structural (bounce→0, sub-debounce→NONE, zero threshold→fallback).

## 13. Allocation

**Statically allocated only — no heap.**

| Allocation | Location | Size | Notes |
|---|---|---|---|
| `TBL[16]` | `src/app/enc_decode.h:39` | 16 × `int8_t` = 16 B | `static const` array inside `encStep()` — ROM-resident (string-literal/const data section on ESP32). |
| `EncDetent` struct | `src/app/input_map.h:108` | 6 B (`uint8_t` + `int16_t` + `uint8_t` + `bool`) | Embedded as `InputMapper::dec` — stack/instance local, not heap. |
| `EncButton` struct | `src/app/enc_decode.h:109-113` | ~19 B | Embedded as `InputMapper::sw` and `InputMapper::btn[4]` (`input_map.h:109-110`) — instance local. |
| `NavQueue` | `src/app/input_map.h:85-100` | 40 B (`ev[8]` + head/tail) | Embedded as `InputMapper::q` (`input_map.h:111`). Fixed-size ring buffer, no dynamic allocation. |

No `malloc`, no `heap_caps_malloc`, no `MALLOC_CAP_*` flags. The entire `app` input stack fits in < 100 bytes of static/struct memory, well within internal DRAM. No PSRAM involvement.

## 14. Timing

| Constraint | Value | Source | Basis |
|---|---|---|---|
| Debounce floor | 25 ms | `src/app/enc_decode.h:95` (`ENC_DEBOUNCE_MS`) | Sub-25 ms blips are contact noise |
| Long-press threshold | 600 ms | `src/app/enc_decode.h:96` (`ENC_LONG_PRESS_MS`) | Standard "hold to distinguish from tap" |
| Poll frequency (target) | ~1 kHz (1 ms) | `input_map.h:3` ("~1 kHz poll a human can't out-run") | Human finger cannot generate > 1000 steps/sec on a standard encoder |
| Quadrature edges per detent | 1/2/4 | `EncDetent::perDetent` (line 61), configurable via `Config.encSteps` | 4 = standard EC11, 2 = half-step, 1 = non-detented raw |
| Press classification | Release-edge | `EncButton::feed` line 127 | Classified on release so long presses are unambiguous |
| Queue capacity | 8 events | `NavQueue::ev[8]` (`input_map.h:86`), masked with `& 7` | Sufficient for ~1000 polls/sec human input |

The quadrature state table decodes transitions instantaneously (lookup in a 16-entry const array). Button debouncing is time-based (`nowMs - lastChangeMs`). All timing is driven by the caller's millisecond tick.

## 15. Traceability

| Claim | Source |
|---|---|
| Pure decode, no Arduino dependency (only `<stdint.h>`) | `src/app/enc_decode.h:6,18` |
| Layered: enc_decode → input_map → menu | `src/app/enc_decode.h:10-13` |
| `encStep` returns +1/-1/0 via 16-entry table | `src/app/enc_decode.h:38-46` |
| Impossible double-bit transitions decode to 0 | `src/app/enc_decode.h:39-43` (all 0 entries) |
| `EncDetent` tracks `prevAB` (0xFF = unseeded) | `src/app/enc_decode.h:59,71` |
| `perDetent` defaults to 4 (standard EC11) | `src/app/enc_decode.h:61` |
| `perDetent == 0` guarded to 1 | `src/app/enc_decode.h:66` |
| `EncDetent::feed` seeds first call, then decodes | `src/app/enc_decode.h:69-81` |
| Detent threshold: `accum >= perDetent` emits +1 | `src/app/enc_decode.h:78` |
| Detent threshold: `accum <= -perDetent` emits -1 | `src/app/enc_decode.h:79` |
| `EncPress` enum: NONE=0, SHORT=1, LONG=2 | `src/app/enc_decode.h:89-93` |
| `ENC_DEBOUNCE_MS = 25` | `src/app/enc_decode.h:95` |
| `ENC_LONG_PRESS_MS = 600` | `src/app/enc_decode.h:96` |
| `encClassifyPress` returns NONE if below debounce | `src/app/enc_decode.h:100-101` |
| `EncClassifyPress` returns LONG if ≥ threshold | `src/app/enc_decode.h:102` |
| `EncButton` edge-debounced tracking fields | `src/app/enc_decode.h:109-113` |
| `EncButton::feed` debounces and classifies on release | `src/app/enc_decode.h:121-131` |
| `heldOut` reports current press duration | `src/app/enc_decode.h:129` |
| `isHolding` returns true once long threshold crossed | `src/app/enc_decode.h:135-137` |
| `TBL[16]` is `static const` — ROM-resident | `src/app/enc_decode.h:39` |
| `InputMapper` consumes `EncDetent` + `EncButton` | `src/app/input_map.h:108-110` |
| `Config.encSteps` schema field (CFG_LIVE) | `src/cfg/config_schema.cpp:75` |
| `Config.encReverse` schema field (CFG_LIVE) | `src/cfg/config_schema.cpp:76` |
| `Config.btnActiveHigh` schema field (CFG_LIVE) | `src/cfg/config_schema.cpp:85` |
| `_base.ini` default `encsteps=4` | `templates/_base.ini:65` |
| Button roles default: btn1act=3 (Next), btn2act=4 (Prev), btn3act=1 (Enter), btn4act=2 (Back) | `templates/_base.ini:67-70` |

## 16. Cross-References

| Module doc | Consumes (this module provides) | Provides to that module |
|---|---|---|
| [Input Map](./app-input-map.md) | `encStep` (`input_map.h:73`), `EncDetent` (`input_map.h:108,119,148`), `EncButton` (`input_map.h:109,110,120-121,154,166`), `EncPress` (`input_map.h:33-39,154,167`), `ENC_LONG_PRESS_MS` (`input_map.h:79,116`) | — (leaf decode layer) |
| [Menu](./app-menu.md) | `NavEvent` enum (provided by Input Map, transitively by this module's `encStep`/`EncButton` classification chain) | — (indirect; menu does not call enc_decode directly) |
| [Include Headers](./include-headers.md) | — | `Config.encSteps` (`include/config_schema.h:51`), `Config.encReverse` (`include/config_schema.h:52`) — the persisted fields that configure `EncDetent` |

No cross-references to `drv`, `cfg` (beyond config field definitions), `core`, `net`, or `sys` layers.

## 17. Limitations

- **Not wired into main.cpp**: Despite being present since issue #24, `enc_decode.h` is not included by `main.cpp` (`src/main.cpp:1-31`). The encoder/button GPIO pins (`cfg.encA/B/Sw`, `cfg.btn1Pin..4`, `config_schema.cpp:72-85`) are defined in config and templates (`_base.ini:65-70`), but no firmware poll task reads them. The header-only module is only reachable transitively through `input_map.h` → `menu.h` — neither of which `main.cpp` includes.
- **No GPIO sampling in this module**: `enc_decode.h` receives already-sampled logical levels (`a`, `b` as `bool`). The caller must handle pin reading, active-high/low inversion at the GPIO level, and timing the poll. This module only classifies the levels.
- **Bounce on simultaneous A+B transition**: the state table encodes the four "impossible" transitions as 0 (lines 39-43), which means a genuine mechanical bounce that flips both A and B simultaneously is silently treated as no motion. For well-behaved detented encoders this is correct; for noisy encoders it can lose steps.
- **`heldOut` is optional**: if a caller passes `nullptr`, the "keep holding" hint is lost (`input_map.h:112` soloButton path and others). No compile-time enforcement.
- **No hardware test path**: this module cannot be tested on real hardware without the firmware poll loop that doesn't exist yet (`main.cpp` does not instantiate `InputMapper`).

## 18. Open Questions

- Not determinable from the inspected source code — where the firmware poll loop that samples encoder A/B GPIO pins and calls `InputMapper::poll()` is intended to live. `main.cpp:133-166` (`loop()`) does not include `input_map.h` or call any input polling function.
- Not determinable from the inspected source code — what the planned encoder GPIO pin numbers are. `Config.encA/encB/encSw` exist (`include/config_schema.h:50,52`) but no template sets them to valid pins (`templates/_base.ini:65-70` only sets `encsteps`, button roles, and `ctlunimax`; encoder pins stay at the neutral `-1`).
- Not determinable from the inspected source code — whether the `InputMapper` singleton is intended to live in the `ledTask` (50 ms period, `tasks.cpp:84`) or in a dedicated input-task on core 0. The 1 kHz target poll (`input_map.h:3`) conflicts with both the 50 ms LED task and the 1 ms DMX tick on core 1.
- Not determinable from the inspected source code — whether `ctlUniMax` (`config_schema.cpp:86`) is the only config field consumed by `menu.h` (it controls the `vmax` of universe VALUE items), or if additional menu-specific fields are planned.
- Not determinable from the inspected source code — whether the app modules are intended to be added to the `[env:unit-test]` `build_src_filter` (`platformio.ini:204-214`) for host testing, since they currently depend on no ESP-IDF APIs.

## 19. Testing

No test coverage for this domain.

- No files in `test/native/` or `test/unit-test/` reference `enc_decode.h`, `InputMapper`, `EncDetent`, `EncButton`, `NavEvent`, or any function from this module.
- The `test/native/shim/` directory provides shims for `Arduino.h`, `Preferences.h`, `esp_log.h`, `esp_err.h`, `esp_heap_caps.h`, `driver/rmt_tx.h`, and `driver/uart.h` — but none are specific to the app input layer.
- The header is designed for host testing (only `<stdint.h>`, all functions `static inline` or POD struct methods — `src/app/enc_decode.h:6`), but no test file includes it. The `seqlock_test.cpp` (`test/native/seqlock_test.cpp:4`), `merge_test.cpp` (`test/native/merge_test.cpp:4-7`), `config_test.cpp` (`test/native/config_test.cpp:2-4`), and `rdm_types_test.cpp` (`test/native/rdm_types_test.cpp:2-3`) do not exercise encoder/button logic.
- The `[env:unit-test]` build_src_filter (`platformio.ini:204-214`) does not include `src/app/` sources, so `pio test -e unit-test` does not compile these modules.

## 20. History

- **Issue #24** (on-unit controls): `enc_decode.h` was created as the bottom layer of a three-tier input stack (`enc_decode.h` → `input_map.h` → `menu.h`), documented at `src/app/enc_decode.h:1-3`. The design rationale is: "Keeping the three layers separate is what makes 'any combination of an encoder and 0..4 buttons' testable without hardware" (`enc_decode.h:12-13`).
- **Config field alignment**: `encSteps` was added to `config_schema.cpp:75` as `CFG_LIVE` with range 1-4 (`config_schema.cpp:75`), matching the `EncDetent` constructor's accepted range (`enc_decode.h:61`). The `_base.ini` template default `encsteps=4` (`templates/_base.ini:65`) matches the `EncDetent` default at `enc_decode.h:61`.
- **Button role defaults**: `_base.ini:67-70` sets `btn1act=3` (ROLE_NEXT → NAV_INC), `btn2act=4` (ROLE_PREV → NAV_DEC), `btn3act=1` (ROLE_ENTER → NAV_ENTER), `btn4act=2` (ROLE_BACK → NAV_BACK) — the four standard menu navigation roles, mirroring the `BtnRole` enum at `input_map.h:44-50`.
- **Solo-button synthesis**: `input_map.h:112-129` and `input_map.h:168-171` implement the fallback that a single button (no encoder) maps short=INC, long=ENTER. This design decision is documented at `enc_decode.h:88` and `input_map.h:16-21`.
