# On-Unit Menu System — Technical Reference

Domain: app.menu

## 1. Domain Scope

This module implements a tiny, generic, on-display menu system driven by navigation events (`NavEvent`). It is a single header file (`src/app/menu.h`) with no dependencies beyond `<stdint.h>`, `<string.h>`, and `input_map.h` (for the `NavEvent` enum).

It owns:
- A flat list of menu items (`MenuItem[MENU_MAX_ITEMS]`), each either a `VALUE` (an integer the user scrolls) or an `ACTION` (a trigger like "Exit").
- A two-mode state machine: `BROWSE` (highlight navigation) and `EDIT` (value adjustment).
- The `MenuResult` struct returned after each nav event, signaling redraw, commit, action-fire, or close.

It delegates:
- Everything about physical input sensing to `input_map.h` (`InputMapper`) — this module receives already-decoded `NavEvent`s.
- All persistence (saving the committed value) to the caller (`main.cpp`): `handle()` returns the committed `value` and the item's `id`; the caller writes it to `Config` / NVS.
- All hardware/display rendering to the caller — `MenuItem.label` is a plain `char[MENU_LABEL_LEN]` buffer; how it gets drawn on an OLED or LED panel is not this module's concern.

It is consumed by:
- **`main.cpp`** (planned, not yet wired) — builds the `MenuItem` list from `cfg`, calls `InputMapper::next()` to get a `NavEvent`, passes it to `Menu::handle()`, then renders `MenuResult` to the display and persists commits. `main.cpp:1-31` does not currently include `menu.h`.

## 2. Layer Mapping

| Layer | Module path | Role here |
|---|---|---|
| **app** | `src/app/menu.h` | On-display menu engine; header-only, pure logic |
| **app** | `src/app/input_map.h` | Provides `NavEvent` enum (included at `menu.h:31`) |
| **app** | `src/app/enc_decode.h` | Indirect dependency — provides `EncDetent`/`EncButton` that produce `NavEvent`s consumed by this menu via `input_map.h` |
| **cfg** | `src/cfg/config_schema.cpp` | `ctlUniMax` (line 86, CFG_LIVE) — the "Menu max universe" config field that sets `MenuItem.vmax` for universe VALUE items |
| **include** | `include/config_schema.h` | `Config.ctlUniMax` (line 56) — feeds `MenuItem.vmax` at menu construction |

## 3. Source Files

| File | Role |
|---|---|
| `src/app/menu.h` | Entire module — 159-line header-only file. No `.cpp`. Contains `MenuMode`, `MenuItemKind`, `MenuItem`, `MenuResult`, `Menu` struct. |
| `src/app/input_map.h` | Provides `NavEvent` enum (line 33 of input_map.h; included at `menu.h:31`) |
| `src/cfg/config_schema.cpp` | `ctlUniMax` field (line 86) — the max universe value for menu VALUE items |

No `.cpp` file exists for this module. All logic is in struct methods within `menu.h`.

## 4. Data Structures

### `MenuMode` — `src/app/menu.h:33`

| Enumerator | Value | Description |
|---|---|---|
| `MENU_CLOSED` | 0 | Menu not active |
| `MENU_BROWSE` | 1 | Navigating between items (highlight mode) |
| `MENU_EDIT` | 2 | Editing a VALUE item (scrolling the value) |

### `MenuItemKind` — `src/app/menu.h:34`

| Enumerator | Value | Description |
|---|---|---|
| `MI_VALUE` | 0 | Scrollable integer (e.g. universe) |
| `MI_ACTION` | 1 | Trigger (e.g. "Exit") |

### Constants — `src/app/menu.h:36-37`

| Constant | Value | Line | Description |
|---|---|---|---|
| `MENU_MAX_ITEMS` | 8 | 36 | Maximum items in the menu |
| `MENU_LABEL_LEN` | 12 | 37 | Label buffer size (including null terminator) |

### `MenuItem` — `src/app/menu.h:39-47`

| Field | Type | Line | Description |
|---|---|---|---|
| `label[12]` | `char` | 40 | Display label (truncated to 11 chars + null) |
| `id` | `uint8_t` | 41 | Caller's tag; echoed in `MenuResult.committedId` / `actionId` |
| `kind` | `uint8_t` | 42 | `MI_VALUE` (0) or `MI_ACTION` (1) |
| `enabled` | `bool` | 43 | If false, item is skipped by navigation and not drawn |
| `value` | `int32_t` | 44 | VALUE: current committed value |
| `vmin` | `int32_t` | 45 | VALUE: minimum (inclusive) |
| `vmax` | `int32_t` | 46 | VALUE: maximum (inclusive); edit wraps `vmin..vmax` |

### `MenuResult` — `src/app/menu.h:51-57`

What `handle()` reports after one event.

| Field | Type | Line | Description |
|---|---|---|---|
| `changed` | `bool` | 52 | `true` if visible state moved → caller should redraw |
| `closed` | `bool` | 53 | `true` if menu just closed (BACK in BROWSE) |
| `committedId` | `int16_t` | 54 | `>=0` if a VALUE was committed; caller reads `value` |
| `actionId` | `int16_t` | 55 | `>=0` if an ACTION fired |
| `value` | `int32_t` | 56 | Committed value (valid when `committedId >= 0`) |

### `Menu` — `src/app/menu.h:59-157`

The menu engine struct.

| Field | Type | Line | Description |
|---|---|---|---|
| `items[8]` | `MenuItem` | 60 | Fixed-size item array |
| `count` | `uint8_t` | 61 | Active item count |
| `mode` | `MenuMode` | 62 | Current state (CLOSED/BROWSE/EDIT) |
| `sel` | `int8_t` | 63 | Highlighted index (always an enabled item while open) |
| `edit` | `int32_t` | 64 | Working value while in EDIT mode |

Methods:
- `clear()` (line 66) — resets all fields.
- `addValue(id, label, v, mn, mx, enabled=true)` (line 68) — appends a VALUE item.
- `addAction(id, label, enabled=true)` (line 75) — appends an ACTION item.
- `enabledCount()` (line 82) — count of enabled items.
- `firstEnabled()` (line 85) — index of first enabled item, or -1.
- `stepEnabled(int from, int dir)` (line 90) — next/prev enabled index with wraparound.
- `open()` (line 102) — enters BROWSE mode, highlights first enabled.
- `close()` (line 107) — sets mode to CLOSED.
- `isOpen()` (line 108) — `mode != MENU_CLOSED`.
- `wrap(v, mn, mx)` (line 110, `static`) — modular wrap into `[mn, mx]`.
- `handle(NavEvent e)` (line 116) — main dispatch: processes one nav event, returns `MenuResult`.

## 5. Concurrency

**Single-threaded by design.** `Menu` holds mutable state (`count`, `mode`, `sel`, `edit`, `items[]`) with no synchronization. It is intended to run in the `loop()` on core 0 alongside the serial console and WebSocket push (`src/main.cpp:133-166`), called once per nav event dequeued from `InputMapper::next()`.

No FreeRTOS primitives, no seqlock, no SPSC ring. The menu is not called from `dmxTxTask` (core 1, priority 19, `src/sys/tasks.cpp:83`) or the RDM task (core 1, priority 18). Input polling and menu handling are both core-0 `loop()` responsibilities — they never cross cores.

Currently, this module is **not included** by `main.cpp` (`src/main.cpp:1-31`), so there is no actual runtime concurrency to analyze.

## 6. State Machine

The menu operates as a two-mode state machine driven by `NavEvent` inputs:

```
                    ┌──────────────────────────────────────────────┐
                    │                                              │
                    ▼                                              │
               ┌──────────┐     NAV_BACK                      │
  MENU_       │  BROWSE   │ ──► close() ──►  MENU_CLOSED        │
  CLOSED      └──────────┘      (closed=true, changed=true)      │
   ▲              │                                              │
   │              │ NAV_ENTER on MI_ACTION                      │
   │              │  r.actionId = id; r.changed = true          │
   │              └──────────┐                                    │
   │                         │                                    │
   │              NAV_ENTER on MI_VALUE                           │
   │                         ▼                                    │
   │               ┌──────────┐                                    │
   │               │  EDIT    │                                    │
   └───────◄──────└──────────┘                                    │
        NAV_BACK              │                                    │
        (discard edit)         │ NAV_ENTER (commit)                │
        r.changed=true         ▼  r.committedId=id                │
        mode=MENU_BROWSE      ┌─────────────────────────────────┐ │
                              │  it.value = edit                 │ │
                              │  r.value = edit                  │ │
                              │  r.committedId = id             │ │
                              │  r.changed = true               │ │
                              │  mode=MENU_BROWSE               │ │
                              └─────────────────────────────────┘ │
                                              │                   │
                              NAV_INC/DEC (change value)          │
                              edit = wrap(edit±1, vmin, vmax)     │
                              r.changed = true                    │
                                              │                   │
                                              └──(stays in EDIT)──┘
```

### State transitions

| From | Event | To | Side effect |
|---|---|---|---|
| `MENU_BROWSE` | `NAV_INC` | `MENU_BROWSE` | `sel = stepEnabled(sel, +1)`; `r.changed = true` |
| `MENU_BROWSE` | `NAV_DEC` | `MENU_BROWSE` | `sel = stepEnabled(sel, -1)`; `r.changed = true` |
| `MENU_BROWSE` | `NAV_ENTER` on `MI_ACTION` | `MENU_BROWSE` | `r.actionId = items[sel].id`; `r.changed = true` |
| `MENU_BROWSE` | `NAV_ENTER` on `MI_VALUE` | `MENU_EDIT` | `edit = items[sel].value`; `r.changed = true` |
| `MENU_BROWSE` | `NAV_BACK` | `MENU_CLOSED` | `close()`; `r.closed = true`; `r.changed = true` |
| `MENU_EDIT` | `NAV_INC` | `MENU_EDIT` | `edit = wrap(edit+1, vmin, vmax)`; `r.changed = true` |
| `MENU_EDIT` | `NAV_DEC` | `MENU_EDIT` | `edit = wrap(edit-1, vmin, vmax)`; `r.changed = true` |
| `MENU_EDIT` | `NAV_ENTER` | `MENU_BROWSE` | `items[sel].value = edit`; `r.committedId = id`; `r.value = edit`; `r.changed = true` |
| `MENU_EDIT` | `NAV_BACK` | `MENU_BROWSE` | Discard edit; `r.changed = true` |
| any | `NAV_NONE` | (same) | No-op; `r` is all-false |

Disabled items (`enabled == false`) are skipped by `stepEnabled()` (line 90-98) and never become `sel`.

## 7. Entry Points

Called by the consumer (`main.cpp` — not yet wired):

| Entry point | First call site (planned) | Line | Caller |
|---|---|---|---|
| `Menu::clear()` | Menu close / reset | `src/app/menu.h:66` | Reset to closed state |
| `Menu::addValue(id, label, v, mn, mx)` | Menu build phase | `src/app/menu.h:68` | Caller populates items before `open()` |
| `Menu::addAction(id, label)` | Menu build phase | `src/app/menu.h:75` | Caller adds Exit and other actions |
| `Menu::open()` | After build | `src/app/menu.h:102` | Enter BROWSE, highlight first enabled |
| `Menu::close()` | Via `NAV_BACK` or caller | `src/app/menu.h:107` | Set mode to CLOSED |
| `Menu::handle(e)` | `loop()` nav dispatch | `src/app/menu.h:116` | Main entry: process one `NavEvent`, return `MenuResult` |
| `Menu::isOpen()` | `loop()` guard | `src/app/menu.h:108` | Check if menu is active before processing |

## 8. Data Flow

1. **Menu build** (caller / `main.cpp`, not yet wired): caller calls `menu.clear()`, then `menu.addValue(id, label, v, vmin, vmax)` for each universe item and `menu.addAction(id, "Exit")` for the exit action, then `menu.open()`. The `vmax` for universe VALUE items comes from `cfg.ctlUniMax` (`config_schema.cpp:86`; `_base.ini:66` defaults to 15).
2. **Nav event dequeue**: caller calls `InputMapper::next()` (`input_map.h:176`) → returns a `NavEvent` or `NAV_NONE`.
3. **Dispatch**: caller passes each `NavEvent` to `Menu::handle(e)` (`menu.h:116`). If `mode == MENU_CLOSED` or `e == NAV_NONE` (line 118), returns an all-false `MenuResult`.
4. **BROWSE mode** (lines 120-138): `NAV_INC`/`NAV_DEC` moves `sel` via `stepEnabled()` (wrapping among enabled items). `NAV_ENTER` either fires an ACTION (sets `actionId`) or enters EDIT (sets `edit = items[sel].value`). `NAV_BACK` closes the menu.
5. **EDIT mode** (lines 141-155): `NAV_INC`/`NAV_DEC` adjusts `edit` via `wrap()` (line 110-114) within `[vmin, vmax]`. `NAV_ENTER` commits: `items[sel].value = edit` (line 146), sets `committedId` and `value` in `MenuResult`. `NAV_BACK` discards the edit and returns to BROWSE.
6. **Render**: caller checks `MenuResult.changed` (line 52) — if true, redraws the menu on the display using `items[sel].label`, highlight `sel`, and (in EDIT mode) the working `edit` value.
7. **Persist**: if `MenuResult.committedId >= 0` (line 54), caller reads `MenuResult.value` and writes it into the relevant `Config` field (e.g. `cfg.outputs[i].universe = result.value`), then calls `cfgcore::save()` (persists to NVS).

## 9. Protocol Layout

N/A (no wire protocol). The menu communicates exclusively through in-memory `NavEvent` values (a `uint8_t` enum from `input_map.h:33-39`) and `MenuResult` return structs. There is no network protocol, serial protocol, or byte-stream format. The `MenuItem.label` field is a plain char buffer for local display rendering.

## 10. Config Integration

This module reads **no** `Config` fields directly — it is a pure menu engine. The only config coupling is at construction time by the caller:

| Config field | Schema line | CFG flags | Used for (by caller) |
|---|---|---|---|
| `ctlUniMax` | `config_schema.cpp:86` | `CFG_LIVE` | Sets `MenuItem.vmax` for universe VALUE items at menu build time (caller passes `cfg.ctlUniMax` as `mx` arg to `addValue`) |

The `_base.ini` template default `ctlunimax=15` (`templates/_base.ini:66`) gives a universe range of 1–15 (the common single-net range). `Config.ctlUniMax` is at `include/config_schema.h:56`.

All other menu item values (current universe, preset index, etc.) come from `Config` fields set by the caller when building the `MenuItem` list — this module does not iterate `Config` directly.

## 11. Lifecycle

| Phase | Function | Call site | Notes |
|---|---|---|---|
| Build | `clear()` + `addValue()` + `addAction()` | Caller (main.cpp loop — not yet wired) | Construct the item list |
| Open | `open()` | `src/app/menu.h:102` | Enter BROWSE mode, highlight first enabled item |
| Run | `handle(NavEvent)` | `src/app/menu.h:116` | Process one event per nav dequeue; called in loop |
| Close | `close()` or `NAV_BACK` in BROWSE | `src/app/menu.h:107,133` | Set mode to MENU_CLOSED |
| Destroy | (none) | — | Stack/instance-local; no teardown |

No FreeRTOS task, no init function in `main.cpp:38-131`. The module is header-only and dormant until `main.cpp` includes `menu.h` and instantiates a `Menu`.

## 12. Error Handling

| Return | Line | Behavior |
|---|---|---|
| `MenuResult` from `handle` | `src/app/menu.h:116` | Returns all-false `MenuResult` if `mode == MENU_CLOSED` or `e == NAV_NONE` (line 118). No explicit error. |
| `void` from `addValue` / `addAction` | lines 68, 75 | Silently returns (no-op) if `count >= MENU_MAX_ITEMS` (lines 69, 76). No error, no overflow detection. |
| `int` from `enabledCount` / `firstEnabled` / `stepEnabled` | lines 82, 85, 90 | `firstEnabled`/`stepEnabled` return -1 if no enabled items; `stepEnabled` returns `from` if it's the only enabled item (line 97). |
| `int32_t` from `wrap` | `src/app/menu.h:110` | If `span <= 0` (vmin > vmax), returns `mn` (line 112). No error. |

No `ESP_LOGE`, no `Serial.printf`, no `esp_err_t`. The silent ignore at `addValue` line 69 / `addAction` line 76 is the only error path — a menu with >8 items silently truncates the overflow.

## 13. Allocation

**Statically allocated, embedded in `Menu` instance — no heap.**

| Allocation | Location | Size | Notes |
|---|---|---|---|
| `items[8]` | `src/app/menu.h:60` | 8 × `MenuItem` ≈ 8 × 22 B ≈ 176 B | Fixed array of `MenuItem` structs |
| `MenuItem.label[12]` | `src/app/menu.h:40` | 12 B | Truncated to 11 chars + null |
| `MenuResult` | `src/app/menu.h:51-57` | ~14 B | Returned by value from `handle()` |

No `malloc`, no `heap_caps_malloc`, no PSRAM. The entire `Menu` struct (with 8 items) fits in ~200 bytes of stack/C++-instance memory.

## 14. Timing

**Best-effort (no hard deadline).**

The menu is driven entirely by human interaction through nav events. There are no time-constrained deadlines:

| Constraint | Value | Source | Basis |
|---|---|---|---|
| Render latency | After next `loop()` iteration | `menu.h:116` | `handle()` is called once per dequeued event; render follows in `loop()` |
| Event processing | Sub-microsecond | `menu.h:116-156` | Pure integer math, no I/O; completes well within a 1 ms `loop()` cycle |
| Debounce/long-press | Governed by `enc_decode.h` (25 ms / 600 ms) | `src/app/enc_decode.h:95-96` | Not this module's concern; nav events arrive already classified |
| Menu build | Synchronous in `loop()` | `menu.h:68,75` | Called once per menu open; ≤ 8 items |

No relationship to the 1 ms `dmxTxTask` tick (`tasks.cpp:142`) or the 2 ms `netRxTask` tick (`tasks.cpp:149`). The menu runs at human-interaction speed on core 0 `loop()`.

## 15. Traceability

| Claim | Source |
|---|---|
| Header-only, only `<stdint.h>`, `<string.h>`, `input_map.h` | `src/app/menu.h:3,30-31` |
| Two modes: BROWSE and EDIT | `src/app/menu.h:6-12` |
| Disabled items skipped by navigation and not drawn | `src/app/menu.h:20-22` |
| Always an Exit ACTION item so lone-button rigs can leave | `src/app/menu.h:24-25` |
| `MenuMode` enum: CLOSED=0, BROWSE=1, EDIT=2 | `src/app/menu.h:33` |
| `MenuItemKind` enum: VALUE=0, ACTION=1 | `src/app/menu.h:34` |
| `MENU_MAX_ITEMS = 8` | `src/app/menu.h:36` |
| `MENU_LABEL_LEN = 12` | `src/app/menu.h:37` |
| `MenuItem` fields: label/id/kind/enabled/value/vmin/vmax | `src/app/menu.h:39-47` |
| `MenuItem.label` truncated to `MENU_LABEL_LEN-1` + null | `src/app/menu.h:71,78` |
| `MenuResult` fields: changed/closed/committedId/actionId/value | `src/app/menu.h:49-57` |
| `Menu` struct: items/count/mode/sel/edit | `src/app/menu.h:59-64` |
| `stepEnabled` wraps with `(i + dir + count) % count` | `src/app/menu.h:94` |
| `stepEnabled` returns `from` if only one enabled | `src/app/menu.h:97` |
| `open()` highlights first enabled | `src/app/menu.h:102-106` |
| `wrap()` modular arithmetic: `((v - mn) % span + span) % span + mn` | `src/app/menu.h:110-114` |
| `handle()` no-op if CLOSED or NAV_NONE | `src/app/menu.h:118` |
| BROWSE: NAV_ENTER on MI_ACTION fires action | `src/app/menu.h:126-127` |
| BROWSE: NAV_ENTER on MI_VALUE enters EDIT | `src/app/menu.h:129` |
| BROWSE: NAV_BACK closes menu | `src/app/menu.h:132-133` |
| EDIT: NAV_ENTER commits `it.value = edit` | `src/app/menu.h:146-147` |
| EDIT: NAV_BACK discards edit | `src/app/menu.h:150-151` |
| `addValue`/`addAction` silently ignore if `count >= MENU_MAX_ITEMS` | `src/app/menu.h:69,76` |
| `ctlUniMax` schema field = "Menu max universe" | `src/cfg/config_schema.cpp:86` |
| `ctlUniMax` default = 15 in `_base.ini` | `templates/_base.ini:66` |
| `Config.ctlUniMax` field | `include/config_schema.h:56` |
| Menu consumes `NavEvent` from `input_map.h` | `src/app/menu.h:31` (include), `menu.h:116` (handle param) |
| Input stack: enc_decode → input_map → menu | `src/app/enc_decode.h:10-11`, `src/app/input_map.h:2-3,21` |

## 16. Cross-References

| Module doc | Consumes (this module provides) | Provides to that module |
|---|---|---|
| [Input Map](./app-input-map.md) | `Menu` consumes `NavEvent` (provided by Input Map's `InputMapper::next()`, `input_map.h:176`); caller passes it to `Menu::handle()` | — |
| [Enc Decode](./app-enc-decode.md) | Indirect: enc_decode produces `NavEvent`s via `input_map.h` which `Menu::handle()` consumes | — |
| [Include Headers](./include-headers.md) | `Config.ctlUniMax` (`include/config_schema.h:56`) — used by caller to set `MenuItem.vmax` | — |
| [Config Engine](./config-engine.md) | `ctlUniMax` field (`config_schema.cpp:86`) — caller reads it to build universe VALUE items | — |
| [Sys Tasks](./sys-tasks.md) | — | `Menu::handle()` is intended to run in `loop()` (core 0), not in `dmxTxTask` (core 1, `tasks.cpp:83`) |

## 17. Limitations

- **Not wired into main.cpp**: `main.cpp` (`src/main.cpp:1-31`) does not include `menu.h` or `input_map.h`. The menu engine exists as a complete, self-contained module but has no runtime invocation. The config field `ctlUniMax` (`config_schema.cpp:86`) and `_base.ini` default (`templates/_base.ini:66`) imply a menu is planned, but no display-rendering code exists in `src/sys/` to present it.
- **Static item capacity**: `MENU_MAX_ITEMS = 8` (`menu.h:36`) is hard-coded. `addValue`/`addAction` silently truncate at 8 with no error return (`menu.h:69,76`). A menu needing >8 items must split across sub-menus.
- **Fixed label length**: `MENU_LABEL_LEN = 12` (`menu.h:37`) truncates labels to 11 characters. Long labels like "Universe C" (11 chars) fit; "Output Universe" (15) does not. No dynamic allocation fallback.
- **No multi-page/scrolling**: the menu is a flat list with no concept of pages or scrolling beyond the 8-item cap. All items must fit in `items[8]`.
- **No persistence on commit**: `handle()` returns the committed value in `MenuResult.value` (line 147) but does **not** write it to `Config` or NVS — the caller must do that. This is by design (`menu.h:14-17`: "the caller then persists"), but there is no caller currently.
- **Single selection cursor**: `sel` is a single `int8_t` (`menu.h:63`) — no multi-select support.
- **No edit-cancel confirmation**: in EDIT mode, `NAV_BACK` silently discards the in-progress edit (`menu.h:150-151`) with no "discard changes?" prompt. For destructive actions this is risky; the caller would need to add a confirmation ACTION item.

## 18. Open Questions

- Not determinable from the inspected source code — where the `Menu` instance is intended to live (global singleton, task-local, or stack in `loop()`). No `extern Menu` declaration exists in any header or `.cpp` file.
- Not determinable from the inspected source code — what display-rendering function consumes `MenuResult`. `src/sys/display.cpp` and `src/sys/led_status.cpp` exist but no menu-rendering code was found in `src/sys/`.
- Not determinable from the inspected source code — how `MenuResult.committedId` maps back to a `Config` field. The caller must maintain an id→config-field mapping table, but none exists in the inspected sources.
- Not determinable from the inspected source code — whether the menu is intended to support sub-menus (drill-down ACTION items that open a child menu). The current design is flat (`MENU_MAX_ITEMS = 8`, no parent/child concept).
- Not determinable from the inspected source code — whether `Menu` is intended to be tested in the `[env:unit-test]` Unity environment. The `build_src_filter` (`platformio.ini:204-214`) does not include `src/app/`.

## 19. Testing

No test coverage for this domain.

- No files in `test/native/` or `test/unit-test/` include `menu.h`, `input_map.h`, or reference `Menu`, `MenuResult`, `MenuItem`, `MenuMode`, `MenuItemKind`, or `handle()`.
- The `[env:unit-test]` `build_src_filter` (`platformio.ini:204-214`) does not include `src/app/` sources — only `src/core/` and `src/cfg/` sources are compiled for the Unity test run.
- The module's header comment claims design intent for host testability ("the caller owns the semantics" at `src/app/menu.h:14-17`), but no test file exercises the `Menu` struct. The existing Unity tests (`test/unit-test/test_config/`, `test_unit_merge/`, `test_unit_seqlock/`, `test_unit_rdm_types/`) cover config, merge, seqlock, and RDM types — not the app input stack.

## 20. History

- **Issue #24** (on-unit controls): `menu.h` was created as the top layer of the three-tier input stack (`enc_decode.h` → `input_map.h` → `menu.h`), documented at `src/app/menu.h:1-4`. The design is "a tiny generic on-display menu driven by nav events" — deliberately minimal and free of hardware/config dependencies to enable host testing.
- **Two-mode design** (BROWSE/EDIT): documented at `src/app/menu.h:6-12`. BROWSE navigates between items; EDIT adjusts a value. ENTER transitions BROWSE→EDIT (for VALUE) or fires an ACTION; BACK in BROWSE closes, BACK in EDIT discards.
- **Disabled-item skipping**: `src/app/menu.h:20-22` documents that disabled items are skipped by navigation and not drawn, so "Output B universe" simply vanishes on a single-output unit. Implemented in `stepEnabled()` (`menu.h:90-98`) and `enabledCount()` (`menu.h:82-84`).
- **Exit item invariant**: `src/app/menu.h:23-25` documents that there is always meant to be an Exit ACTION item, so a unit with only one button (which can't produce BACK) can still leave the menu via `NAV_ENTER` on the Exit action.
- **ctlUniMax config coupling**: the `ctlUniMax` field was added to `config_schema.cpp:86` (`CFG_LIVE`, "Menu max universe") with `_base.ini:66` defaulting to 15, specifically to drive `MenuItem.vmax` for universe selection VALUE items. This is the only direct config touchpoint for the menu system.
