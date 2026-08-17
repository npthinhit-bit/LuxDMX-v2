# Input Mapping — Technical Reference

Domain: app.input-map

## 1. Domain Scope

This module translates raw, physical input signals (quadrature encoder A/B levels, encoder push-button level, and up to four independent button levels) into a tiny alphabet of abstract navigation events that the menu layer understands: `NAV_INC`, `NAV_DEC`, `NAV_ENTER`, `NAV_BACK`.

It owns:
- The `NavEvent` enum and `NavQueue` event buffer.
- The `InputConfig` static description and `InputSample` per-tick snapshot.
- The `InputMapper` engine that runs debouncing, detent accumulation, and role-to-nav mapping with long-press synthesis.

It delegates:
- Raw quadrature decoding to `enc_decode.h` (`EncDetent::feed`, `encStep`).
- Raw button debouncing and press classification to `enc_decode.h` (`EncButton::feed`, `EncPress`).
- All hardware pin reading to the caller (firmware poll loop in `main.cpp` — not yet wired).

It is consumed by:
- **`menu.h`** (line 31) — includes `input_map.h` for the `NavEvent` enum. The menu calls `InputMapper::next()` to dequeue nav events and passes each to `Menu::handle()`.

The design goal (`src/app/input_map.h:14-21`) is that "every combination of an encoder and 0..4 buttons" produces a usable result, via long-press synthesis that fills in missing primitives. `BACK` is never strictly required because the menu always carries an Exit `ACTION` item.

## 2. Layer Mapping

| Layer | Module path | Role here |
|---|---|---|
| **app** | `src/app/input_map.h` | Physical-to-abstract input mapping; header-only, pure logic |
| **app** | `src/app/enc_decode.h` | Provides `EncDetent`, `EncButton`, `EncPress`, `ENC_LONG_PRESS_MS` (included at line 31) |
| **app** | `src/app/menu.h` | Consumer — includes `input_map.h` for `NavEvent` (line 31) |
| **cfg** | `src/cfg/config_schema.cpp` | `encA` (line 72), `encB` (line 73), `encSw` (line 74), `encSteps` (line 75), `encReverse` (line 76), `btn1Pin` (line 77), `btn1Act` (line 78), `btn2Pin` (line 79), `btn2Act` (line 80), `btn3Pin` (line 81), `btn3Act` (line 82), `btn4Pin` (line 83), `btn4Act` (line 84), `btnActiveHigh` (line 85), `ctlUniMax` (line 86) |
| **include** | `include/config_schema.h` | `Config.encA/B/Sw` (lines 50, 51, 52), `Config.encSteps` (line 51), `Config.encReverse` (line 52), `Config.btn1Pin..4` (line 53), `Config.btn1Act..4` (line 54), `Config.btnActiveHigh` (line 55), `Config.ctlUniMax` (line 56) |

## 3. Source Files

| File | Role |
|---|---|
| `src/app/input_map.h` | Entire module — 193-line header-only file. No `.cpp`. Contains `NavEvent`, `BtnRole`, `InputSample`, `InputConfig`, `NavQueue`, `InputMapper`. |
| `src/app/enc_decode.h` | Dependency — provides `EncDetent`, `EncButton`, `EncPress`, `ENC_LONG_PRESS_MS`, `ENC_DEBOUNCE_MS` |
| `src/app/menu.h` | Consumer — includes `input_map.h` (line 31) to get `NavEvent` |
| `src/cfg/config_schema.cpp` | Supplies config field definitions that `InputConfig` is built from (lines 72-86) |

No `.cpp` file exists for this module. All logic is in struct methods within `input_map.h`.

## 4. Data Structures

### `NavEvent` — `src/app/input_map.h:33-39`

| Enumerator | Value | Line | Description |
|---|---|---|---|
| `NAV_NONE` | 0 | 34 | No event (default/no-op) |
| `NAV_INC` | 1 | 35 | Increment / next / scroll up (encoder CW, "next" button) |
| `NAV_DEC` | 2 | 36 | Decrement / previous / scroll down (encoder CCW, "prev" button) |
| `NAV_ENTER` | 3 | 37 | Select / drill in / confirm |
| `NAV_BACK` | 4 | 38 | Cancel / leave |

### `BtnRole` — `src/app/input_map.h:44-50`

What a configured button does. Order == stored config enum value (`input_map.h:41-42`).

| Enumerator | Value | Line | Description |
|---|---|---|---|
| `ROLE_OFF` | 0 | 45 | Ignored even if wired |
| `ROLE_ENTER` | 1 | 46 | Short = ENTER, long = BACK |
| `ROLE_BACK` | 2 | 47 | Always BACK (long or short) |
| `ROLE_NEXT` | 3 | 48 | Short = INC, long = ENTER |
| `ROLE_PREV` | 4 | 49 | Short = DEC, long = ENTER |

### `INPUT_MAX_BTN` — `src/app/input_map.h:52`

`static constexpr int = 4`. Maximum number of separate buttons supported.

### `InputSample` — `src/app/input_map.h:57-64`

One raw sample of every input, taken by the firmware poll and handed to `InputMapper::poll()`.

| Field | Type | Line | Description |
|---|---|---|---|
| `encPresent` | `bool` | 58 | `true` if A/B wired → decode rotation |
| `a` | `uint8_t` | 59 | Raw A level (`a ? 1 : 0`) |
| `b` | `uint8_t` | 59 | Raw B level (`b ? 1 : 0`) |
| `swPresent` | `bool` | 60 | `true` if encoder push button is wired |
| `swLevel` | `bool` | 61 | Raw level of the push pin |
| `btnPresent[4]` | `bool` | 62 | Per-button presence flags |
| `btnLevel[4]` | `bool` | 63 | Raw levels for each button (`true` = HIGH) |

### `InputConfig` — `src/app/input_map.h:67-80`

Static description of a unit's controls, built once from `Config` at init.

| Field | Type | Line | Description |
|---|---|---|---|
| `hasEncoder` | `bool` | 69 | Encoder A/B wired? |
| `encSteps` | `uint8_t` | 70 | Quadrature edges per detent (1/2/4) |
| `encReverse` | `bool` | 71 | Flip CW/CCW |
| `hasSw` | `bool` | 73 | Encoder push-button wired? |
| `role[4]` | `BtnRole` | 75 | Configured role per button (from `Config.btn1Act..4`) |
| `btnPresent[4]` | `bool` | 76 | Which of the 4 buttons have a pin assigned |
| `activeHigh` | `bool` | 78 | Buttons + push: `true` = pressed == HIGH (default: pressed == LOW) |
| `longMs` | `uint32_t` | 79 | Long-press threshold (default `ENC_LONG_PRESS_MS`) |

### `NavQueue` — `src/app/input_map.h:85-100`

Fixed-size lossless event queue (8-entry ring buffer).

| Field | Type | Line | Description |
|---|---|---|---|
| `ev[8]` | `NavEvent` | 86 | Ring buffer payload |
| `head` | `uint8_t` | 87 | Dequeue index |
| `tail` | `uint8_t` | 87 | Enqueue index |

Methods:
- `push(NavEvent e)` (line 88) — drops `NAV_NONE`, drops if full (line 91).
- `pop()` (line 94) — returns `NAV_NONE` if empty.
- `empty()` (line 99).

Masked with `& 7` (line 90, 96) for the ring buffer modulo.

### `InputMapper` — `src/app/input_map.h:106-191`

The engine struct that ties decode primitives to config and produces nav events.

| Field | Type | Line | Description |
|---|---|---|---|
| `cfg` | `InputConfig` | 107 | Static control description |
| `dec` | `EncDetent` | 108 | Per-detent quadrature accumulator (from `enc_decode.h`) |
| `sw` | `EncButton` | 109 | Encoder push-button debouncer |
| `btn[4]` | `EncButton` | 110 | Per-button debouncers |
| `q` | `NavQueue` | 111 | Event queue |
| `soloButton` | `bool` | 112 | `true` if exactly one actuator and no rotation → short=INC, long=ENTER |

Methods:
- `begin(const InputConfig& c)` (line 117) — stores config, constructs decode primitives, computes `soloButton`.
- `canSelect()` (line 134) — returns `true` if any way to trigger ENTER exists.
- `hasAnyInput()` (line 140) — `hasEncoder || canSelect()`.
- `pressed(bool rawLevel)` (line 143) — applies active-high/low inversion.
- `poll(const InputSample& s, uint32_t now)` (line 145) — main entry: decodes rotation, encoder push, and all buttons; pushes nav events to `q`.
- `next()` (line 176) — drains one event from `q`.
- `mapRole(BtnRole r, EncPress p)` (line 181, `static`, private) — role + press length → `NavEvent` with long-press synthesis.

## 5. Concurrency

**Single-threaded by design.** `InputMapper` holds mutable state (`dec.prevAB`, `dec.accum`, `sw.stable`, `btn[i].stable`, `q.head/tail`) with no synchronization. The design intent (`src/app/input_map.h:3,104`) is that the caller polls all inputs synchronously in a single task and calls `poll()` once per tick.

The intended poll target is the firmware `loop()` on core 0 (`src/main.cpp:133-166`), but this module is **not yet included** by `main.cpp` (no `#include "input_map.h"` at `main.cpp:1-30`). The ~1 kHz poll target (`input_map.h:3`) would conflict with the 1 ms `dmxTxTask` on core 1 (`tasks.cpp:142`) — but since input polling is sub-microsecond (table lookup + comparison), it is intended to run alongside the serial console and WebSocket handling in `loop()` on core 0.

No FreeRTOS primitives, no seqlock, no SPSC ring. The `NavQueue` is a simple ring buffer with no atomicity guarantees — acceptable for single-producer (poll) / single-consumer (menu drain) on one core.

## 6. State Machine

`InputMapper` has no explicit top-level state machine. It contains three independent stateful sub-machines (all in `enc_decode.h`):

| Sub-tracker | State machine | Governed by |
|---|---|---|
| `EncDetent dec` | Seeded → Tracking → No-edge | `src/app/enc_decode.h:69-81` |
| `EncButton sw` | Idle → Debounce → Pressed → Released | `src/app/enc_decode.h:121-131` |
| `EncButton btn[4]` | Same as `sw`, per button | `src/app/enc_decode.h:121-131` |

The `soloButton` flag (`input_map.h:112`) is a one-time computed mode (not a transition state) that changes the mapping table inside `poll()` (lines 155-161, 168-171): in solo mode, the encoder push and lone button map short=INC, long=ENTER instead of the normal short=ENTER, long=BACK.

## 7. Entry Points

All entry points are called by the consumer (`menu.h` / `main.cpp` poll loop — not yet wired):

| Entry point | First call site (planned) | Line | Caller |
|---|---|---|---|
| `InputMapper::begin(c)` | Firmware init (not yet called) | `src/app/input_map.h:117` | Should be called once after config load to construct decode primitives from `InputConfig` |
| `InputMapper::poll(s, now)` | Firmware `loop()` (not yet called) | `src/app/input_map.h:145` | Main per-tick entry: decodes rotation, encoder push, and buttons |
| `InputMapper::next()` | `Menu::handle()` drain loop | `src/app/input_map.h:176` | Called by menu to dequeue `NavEvent`s |
| `InputMapper::canSelect()` | Firmware init (not yet called) | `src/app/input_map.h:134` | Guard: if false, caller should use display-less mode |
| `InputMapper::hasAnyInput()` | Firmware init (not yet called) | `src/app/input_map.h:140` | Guard: if false, skip menu entirely |
| `InputMapper::pressed(rawLevel)` | Inside `poll()` | `src/app/input_map.h:143,154,166` | Internal helper for active-high/low polarity |
| `InputMapper::mapRole(r, p)` | Inside `poll()` | `src/app/input_map.h:172` | Private static — maps role+press to nav event |

## 8. Data Flow

1. **Config → InputConfig**: at init, `InputConfig` is built from `Config` fields: `encA/encB/encSw` determine `hasEncoder`/`hasSw` (`include/config_schema.h:50-52`), `encSteps`/`encReverse` → `InputConfig.encSteps`/`encReverse` (lines 70-71), `btn1Pin..4` → `btnPresent[i]` and `btn1Act..4` → `role[i]` (`input_map.h:75-76,78`), `btnActiveHigh` → `activeHigh` (line 78), `longMs` defaults to `ENC_LONG_PRESS_MS` (line 79).
2. **`InputMapper::begin(cfg)`** (`input_map.h:117-130`): stores config, constructs `EncDetent(c.encSteps, c.encReverse)` (line 119) and `EncButton(c.longMs)` for sw + 4 buttons (lines 120-121), computes `soloButton` (lines 126-129): true if `!hasEncoder && actuators == 1`.
3. **Poll tick**: firmware reads raw GPIO levels (not in this module), builds `InputSample` (`input_map.h:57-64`), calls `InputMapper::poll(s, now)` (line 145).
4. **Rotation decode** (`input_map.h:147-151`): if `cfg.hasEncoder && s.encPresent`, calls `dec.feed(s.a, s.b)` → returns +1/-1/0 per detent. Pushes `NAV_INC` (step>0, line 149) or `NAV_DEC` (step<0, line 150).
5. **Encoder push** (`input_map.h:153-162`): if `cfg.hasSw && s.swPresent`, calls `sw.feed(pressed(s.swLevel), now)` → returns `EncPress`. In normal mode: SHORT→`NAV_ENTER` (line 159), LONG→`NAV_BACK` (line 160). In solo mode: SHORT→`NAV_INC` (line 156), LONG→`NAV_ENTER` (line 157).
6. **Buttons** (`input_map.h:164-173`): for each `i` in 0..3, if `btnPresent[i] && s.btnPresent[i] && role[i] != ROLE_OFF`, calls `btn[i].feed(pressed(s.btnLevel[i]), now)`. In solo mode: SHORT→`NAV_INC`, LONG→`NAV_ENTER` (line 169). Otherwise: `q.push(mapRole(cfg.role[i], p))` (line 172).
7. **`mapRole` synthesis** (`input_map.h:181-190`): maps `BtnRole` + `EncPress` to `NavEvent` with long-press fallback:
   - `ROLE_NEXT` (3): short→INC, long→ENTER
   - `ROLE_PREV` (4): short→DEC, long→ENTER
   - `ROLE_ENTER` (1): short→ENTER, long→BACK
   - `ROLE_BACK` (2): always BACK
8. **Menu consumption**: caller calls `InputMapper::next()` (`input_map.h:176`) → `q.pop()` → `NavQueue::pop` (`input_map.h:94`) → passes `NavEvent` to `Menu::handle()` (`menu.h:116`).

## 9. Protocol Layout

N/A (no wire protocol). This module produces abstract in-memory `NavEvent` enum values (`uint8_t`) that are consumed by `Menu::handle()`. There is no network protocol, serial protocol, or byte-stream format. The `InputSample` structure (`input_map.h:57-64`) is an in-memory representation of GPIO levels, not a wire protocol.

## 10. Config Integration

This module's `InputConfig` is built from `Config` fields. All relevant fields are defined in `config_schema.cpp` (lines 72-86):

| Config field | Schema line | CFG flags | Maps to (InputConfig) |
|---|---|---|---|
| `encA` | `config_schema.cpp:72` | `CFG_REBOOT` | Pin number (caller reads GPIO; not stored in `InputConfig`) |
| `encB` | `config_schema.cpp:73` | `CFG_REBOOT` | Pin number (caller reads GPIO) |
| `encSw` | `config_schema.cpp:74` | `CFG_REBOOT` | Pin number (caller reads GPIO) |
| `encSteps` | `config_schema.cpp:75` | `CFG_LIVE` | `InputConfig.encSteps` (line 70) |
| `encReverse` | `config_schema.cpp:76` | `CFG_LIVE` | `InputConfig.encReverse` (line 71) |
| `btn1Pin` | `config_schema.cpp:77` | `CFG_REBOOT` | Pin presence → `btnPresent[0]` (line 76) |
| `btn1Act` | `config_schema.cpp:78` | `CFG_LIVE` | `role[0]` (line 75) |
| `btn2Pin` | `config_schema.cpp:79` | `CFG_REBOOT` | → `btnPresent[1]` |
| `btn2Act` | `config_schema.cpp:80` | `CFG_LIVE` | `role[1]` |
| `btn3Pin` | `config_schema.cpp:81` | `CFG_REBOOT` | → `btnPresent[2]` |
| `btn3Act` | `config_schema.cpp:82` | `CFG_LIVE` | `role[2]` |
| `btn4Pin` | `config_schema.cpp:83` | `CFG_REBOOT` | → `btnPresent[3]` |
| `btn4Act` | `config_schema.cpp:84` | `CFG_LIVE` | `role[3]` |
| `btnActiveHigh` | `config_schema.cpp:85` | `CFG_LIVE` | `InputConfig.activeHigh` (line 78) |
| `ctlUniMax` | `config_schema.cpp:86` | `CFG_LIVE` | Not consumed by `input_map.h` — consumed by `menu.h` (controls `MenuItem.vmax` for universe VALUE items, `menu.h:73`) |

Button act fields use `ENUM_BTNROLE` (`config_schema.cpp:18`) with labels `{"off", "Enter / Select", "Back", "Next (+)", "Prev (-)"}` — matching `BtnRole` enums at `input_map.h:44-50`. The `_base.ini` template defaults `btn1act=3` (Next), `btn2act=4` (Prev), `btn3act=1` (Enter), `btn4act=2` (Back) (`templates/_base.ini:67-70`).

The `soloButton` mode is computed at runtime in `begin()` (lines 126-129), not from a config field — it derives from `hasEncoder` and the count of configured (present + non-ROLE_OFF) buttons.

## 11. Lifecycle

| Phase | Function | Call site | Notes |
|---|---|---|---|
| Construct | `InputMapper(const InputConfig& c)` | Not yet called in firmware | Constructor at line 114-115 calls `begin(c)` |
| Init | `InputMapper::begin(c)` | Not yet called in firmware | Stores config, constructs `EncDetent`/`EncButton` instances, computes `soloButton` |
| Poll | `InputMapper::poll(s, now)` | Not yet called in firmware | Per-tick: decode rotation → encoder push → buttons → push nav events |
| Drain | `InputMapper::next()` | Called by menu layer | Returns one `NavEvent` or `NAV_NONE` |
| Destroy | (none) | — | Stack/RAM instance; no teardown |

No FreeRTOS task, no init function registered in `main.cpp:38-131`. The module is header-only and dormant until `main.cpp` includes `input_map.h` and instantiates an `InputMapper`.

## 12. Error Handling

| Return | Line | Behavior |
|---|---|---|
| `void` from `push` | `src/app/input_map.h:88` | Silently drops `NAV_NONE` (line 89). If queue full (8 events), drops the event without notification (line 91). |
| `NavEvent` from `pop` | `src/app/input_map.h:94` | Returns `NAV_NONE` if empty (line 95). |
| `NavEvent` from `mapRole` | `src/app/input_map.h:181` | Returns `NAV_NONE` for `ROLE_OFF` and default case (line 188). |
| `bool` from `canSelect` | `src/app/input_map.h:134` | Returns false if no `hasSw` and no configured button is present+non-off. |
| `bool` from `hasAnyInput` | `src/app/input_map.h:140` | `hasEncoder \|\| canSelect()`. |
| `int8_t` from `EncDetent::feed` | `src/app/enc_decode.h:69` | Returns 0 for no-edge/no-motion (lines 72, 80). No error path. |
| `EncPress` from `EncButton::feed` | `src/app/enc_decode.h:121` | Returns `ENC_PRESS_NONE` when no transition. No error path. |

No `ESP_LOGE`, no `Serial.printf`, no `esp_err_t`. The queue-full drop at `input_map.h:91` is the only silent error — the caller cannot detect that a nav event was lost. The design comment at line 83-84 acknowledges this: "8 is plenty at a ~1 kHz poll (a human can't out-run it)."

## 13. Allocation

**Statically allocated, embedded in `InputMapper` instance — no heap.**

| Allocation | Location | Size | Notes |
|---|---|---|---|
| `NavQueue::ev[8]` | `src/app/input_map.h:86` | 8 × `uint8_t` = 8 B | Fixed ring buffer |
| `InputMapper::dec` | `src/app/input_map.h:108` | ~6 B | `EncDetent` struct (see `app-enc-decode.md` §13) |
| `InputMapper::sw` | `src/app/input_map.h:109` | ~19 B | `EncButton` struct |
| `InputMapper::btn[4]` | `src/app/input_map.h:110` | ~76 B | 4 × `EncButton` |
| `InputMapper::q` | `src/app/input_map.h:111` | ~12 B | `NavQueue` |
| `InputConfig` | `src/app/input_map.h:107` | ~16 B | Copied from caller's `Config` snapshot |

Total `InputMapper` footprint ≈ 120 bytes of static/C++-instance memory. No `malloc`, no `heap_caps_malloc`, no PSRAM. The `soloButton` flag (line 112) is a single `bool`.

## 14. Timing

| Constraint | Value | Source | Basis |
|---|---|---|---|
| Poll frequency (target) | ~1 kHz (1 ms) | `src/app/input_map.h:3` | "a human can't out-run it" — sufficient for 1000 detents/sec |
| Debounce floor | 25 ms | `enc_decode.h:95` | Sub-25 ms blips are noise |
| Long-press threshold | 600 ms | `enc_decode.h:96` | Standard hold-to-distinguish |
| Queue drain | After poll, before menu redraw | `input_map.h:176` (`next()`) | Drain all queued events per poll cycle |
| Rotation step | 4 quadrature edges / detent (default) | `EncDetent::perDetent` (line 61) | Standard EC11; configurable via `Config.encSteps` |

No hard real-time deadlines. Input polling is best-effort within the `loop()` cycle (`main.cpp:133`), alongside serial console and WebSocket handling. The debounce and long-press thresholds are human-scale (25 ms, 600 ms) — orders of magnitude slower than the 1 ms DMX tick on core 1.

## 15. Traceability

| Claim | Source |
|---|---|
| Header-only, only `<stdint.h>` + `enc_decode.h` | `src/app/input_map.h:3,30` |
| Three-layer stack: enc_decode → input_map → menu | `src/app/input_map.h:2-3,10-11` |
| Every combination of encoder + 0..4 buttons produces a result | `src/app/input_map.h:14-21` |
| BACK never strictly required (menu has Exit item) | `src/app/input_map.h:20-21` |
| Active-low default, activeHigh flips polarity | `src/app/input_map.h:23-25` |
| `NavEvent` enum values: NONE=0, INC=1, DEC=2, ENTER=3, BACK=4 | `src/app/input_map.h:33-39` |
| `BtnRole` order == stored config enum value | `src/app/input_map.h:41-42` |
| `BtnRole` enum: OFF=0, ENTER=1, BACK=2, NEXT=3, PREV=4 | `src/app/input_map.h:44-50` |
| `INPUT_MAX_BTN = 4` | `src/app/input_map.h:52` |
| `InputSample` fields: encPresent/a/b, swPresent/swLevel, btnPresent/btnLevel | `src/app/input_map.h:57-64` |
| `btnLevel` defaults to `true` (HIGH = un-pressed for active-low) | `src/app/input_map.h:63` |
| `InputConfig` fields match `Config` struct | `src/app/input_map.h:67-80` vs `include/config_schema.h:50-56` |
| `longMs` defaults to `ENC_LONG_PRESS_MS` | `src/app/input_map.h:79` |
| `NavQueue` is 8-entry ring, masked with `& 7` | `src/app/input_map.h:86,90,96` |
| Queue drops `NAV_NONE` silently | `src/app/input_map.h:89` |
| Queue full → drop without notification | `src/app/input_map.h:91` |
| `InputMapper` holds one `EncDetent` + one `EncButton` (sw) + 4 `EncButton` (btns) | `src/app/input_map.h:108-110` |
| `soloButton` computed in `begin()` | `src/app/input_map.h:112,126-129` |
| `begin()` constructs decode primitives from config | `src/app/input_map.h:117-130` |
| `soloButton`: `!hasEncoder && actuators == 1` | `src/app/input_map.h:129` |
| `canSelect()`: sw present or any button present+non-off | `src/app/input_map.h:134-138` |
| `hasAnyInput()`: `hasEncoder \|\| canSelect()` | `src/app/input_map.h:140` |
| `pressed()` applies activeHigh inversion | `src/app/input_map.h:143` |
| `poll()`: rotation → encoder push → buttons | `src/app/input_map.h:145-174` |
| Encoder push: normal short=ENTER/long=BACK | `src/app/input_map.h:158-161` |
| Encoder push: solo short=INC/long=ENTER | `src/app/input_map.h:155-157` |
| Solo button: short=INC/long=ENTER | `src/app/input_map.h:168-171` |
| `mapRole`: ROLE_NEXT short→INC, long→ENTER | `src/app/input_map.h:184` |
| `mapRole`: ROLE_PREV short→DEC, long→ENTER | `src/app/input_map.h:185` |
| `mapRole`: ROLE_ENTER short→ENTER, long→BACK | `src/app/input_map.h:186` |
| `mapRole`: ROLE_BACK always BACK | `src/app/input_map.h:187` |
| `next()` delegates to `q.pop()` | `src/app/input_map.h:176` |
| `btn1Act` schema field (ENUM_BTNROLE, CFG_LIVE) | `src/cfg/config_schema.cpp:78` |
| `btnActiveHigh` schema field (CFG_LIVE) | `src/cfg/config_schema.cpp:85` |
| `ctlUniMax` schema field (CFG_LIVE, "Menu max universe") | `src/cfg/config_schema.cpp:86` |
| `_base.ini` button role defaults | `templates/_base.ini:67-70` |
| `_base.ini` default `encsteps=4` | `templates/_base.ini:65` |
| `Config.encSteps` / `encReverse` fields | `include/config_schema.h:51-52` |
| `Config.btn1Pin..4` / `btn1Act..4` fields | `include/config_schema.h:53-54` |

## 16. Cross-References

| Module doc | Consumes (this module provides) | Provides to that module |
|---|---|---|
| [Enc Decode](./app-enc-decode.md) | `encStep` (used by `EncDetent::feed`), `EncDetent` (`input_map.h:108,119,148`), `EncButton` (`input_map.h:109,110,120-121,143,154,166`), `EncPress` (`input_map.h:33-39,154,167`), `ENC_LONG_PRESS_MS` (`input_map.h:79,116`), `ENC_DEBOUNCE_MS` (via `EncButton::feed`) | — (consumer of enc_decode) |
| [Menu](./app-menu.md) | `NavEvent` enum (`menu.h:31`), `InputMapper::next()` (via `InputConfig`/`InputSample` construction and `poll()`) | — (consumer of nav events from mapper) |
| [Include Headers](./include-headers.md) | — | `Config.encA/B/Sw/encSteps/encReverse/btn1Pin.../btnActiveHigh/ctlUniMax` (`include/config_schema.h:50-56`) — the persisted fields that feed `InputConfig` |
| [Config Engine](./config-engine.md) | `Config` struct fields for controls (lines 72-86 of `config_schema.cpp`) | — (config field definitions consumed by `InputConfig`) |

## 17. Limitations

- **Not wired into main.cpp**: `input_map.h` is not included by `main.cpp` (`src/main.cpp:1-30`). The `InputMapper` is never instantiated in the firmware, and `poll()` is never called. The config fields for encoder/buttons (`config_schema.cpp:72-86`) are fully defined but the GPIO sampling and nav dispatch loop in `loop()` (`main.cpp:133-166`) does not exist.
- **Queue-full silent drop**: `NavQueue::push` at `src/app/input_map.h:88-93` silently drops events when the 8-entry ring buffer is full. At ~1 kHz polling with human input, this is practically impossible, but the drop is undetectable by the caller.
- **No GPIO pin reading in this module**: `InputConfig` stores button roles and `btnPresent`, but the actual pin numbers (`Config.encA/encB/encSw`, `btn1Pin..4`) are not stored in `InputConfig`. The caller (`main.cpp`) must read GPIO levels and construct `InputSample` — this module has no hardware access code path.
- **No hardware test path**: neither `test/native/` nor `test/unit-test/` tests `input_map.h` or `InputMapper`. The `[env:unit-test]` `build_src_filter` (`platformio.ini:204-214`) does not include `src/app/` sources.
- **Encoder presence inference**: `hasEncoder` is a config struct field (`InputConfig.hasEncoder`, line 69), not derived from pin presence. If `hasEncoder` is `true` but no pins are wired, `poll()` will call `dec.feed(1, 1)` (the `InputSample` default levels) with no meaningful result.

## 18. Open Questions

- Not determinable from the inspected source code — where in `main.cpp::loop()` the `InputMapper::poll()` call is intended to be placed. `loop()` (`src/main.cpp:133-166`) handles serial console, WebSocket push, and input router polling, but does not reference `input_map.h` or `menu.h`.
- Not determinable from the inspected source code — whether `Menu` and `InputMapper` instances are intended to be singletons (global) or task-local. No `extern` declaration or static instance exists in any `.cpp` file.
- Not determinable from the inspected source code — whether the display/LED layer that renders the menu is intended to be a new module or integrated into `sys/led_status.cpp` or `sys/display.cpp`. The `cfg.ctlUniMax` field (`config_schema.cpp:86`) strongly implies a menu is planned, but no display-rendering code for the `Menu` struct exists in `src/sys/`.
- Not determinable from the inspected source code — whether the ~1 kHz poll target (`input_map.h:3`) is achievable alongside the WebSocket push (~10 Hz, `main.cpp:158-161`) and serial console (`cfgserial::poll`, `main.cpp:135`) in the `loop()` on core 0 without starving network I/O.

## 19. Testing

No test coverage for this domain.

- No files in `test/native/` or `test/unit-test/` include `input_map.h` or reference `InputMapper`, `InputConfig`, `InputSample`, `NavEvent`, `NavQueue`, `BtnRole`, `mapRole`, or `poll()`.
- The `test/native/shim/` shims (`Arduino.h`, `Preferences.h`, etc.) are not utilized by any input-mapping test.
- The `[env:unit-test]` `build_src_filter` (`platformio.ini:204-214`) does not include `src/app/` — only `src/core/` and `src/cfg/` sources are compiled for the Unity test run.
- The module's header comment claims host-testability ("Pure / host-tested (only `<stdint.h>` + `enc_decode.h`)" at `src/app/input_map.h:3`), but no test file exercises it.

## 20. History

- **Issue #24** (on-unit controls): `input_map.h` was created as the middle layer of the three-tier input stack (`enc_decode.h` → `input_map.h` → `menu.h`), documented at `src/app/input_map.h:1-5`. The layering rationale: "Keeping the three layers separate is what makes 'any combination of an encoder and 0..4 buttons' testable without hardware" (`input_map.h:12-13`).
- **Long-press synthesis design**: The mapping table at `input_map.h:181-190` (`mapRole`) implements the synthesis that lets sparse button sets reach all four nav primitives. The comment at lines 15-21 documents the specific synthesis rules: encoder push short=ENTER/long=BACK; Next/Prev short=INC/DEC/long=ENTER; Enter short=ENTER/long=BACK; single button short=INC/long=ENTER.
- **Solo-button mode**: `soloButton` (`input_map.h:112,126-129`) was added so a unit with exactly one button and no encoder can still navigate the full menu (INC to move, ENTER to select, Exit ACTION to leave). Implemented in `poll()` at lines 155-161 and 168-171.
- **Active-low default**: `activeHigh` defaults to `false` (`input_map.h:78`), matching the standard pull-up-to-GND button wiring (`templates/_base.ini:65-70` does not override it — buttons in `_base.ini` are not assigned pins, staying at neutral `-1`).
- **NavQueue capacity**: The 8-entry ring buffer (`input_map.h:86`) with `& 7` masking (`input_map.h:90,96`) was chosen for ~1 kHz human input with ample headroom, as documented at line 83-84.
