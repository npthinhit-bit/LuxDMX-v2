# Interactive Menu Specification

Domain: app.menu

## 1. Module Overview

The Interactive Menu module provides an on-device, display-driven interactive interface for browsing and editing configuration fields. It is driven by navigation events (NAV_INC, NAV_DEC, NAV_ENTER, NAV_BACK) produced by the input mapping layer, which may originate from either a quadrature encoder with buttons or from serial console commands.

The menu presents a flat list of up to eight items, each either a VALUE item (a scalar that the user scrolls through a bounded range) or an ACTION item (a trigger such as Exit). The menu operates as a two-mode state machine: BROWSE mode highlights items for navigation, and EDIT mode adjusts the value of a selected VALUE item.

The module owns the item list structure (labels, types, ranges, enabled state), the two-mode state machine, the modular value wrapping logic, and the result reporting struct that signals redraw, commit, action-fire, or close events to the caller. It delegates all persistence (saving committed values to non-volatile storage) and all display rendering to the caller.

It is consumed by the firmware main loop, which builds the item list from the current configuration, polls for navigation events, dispatches each event to the menu, renders the result on the display, and persists committed values.
## 2. External Interfaces

### Navigation Event Input

| Event | BROWSE mode behavior | EDIT mode behavior |
|---|---|---|
| NAV_INC | Move highlight to next enabled item | Increment the edit value (wrap within range) |
| NAV_DEC | Move highlight to previous enabled item | Decrement the edit value (wrap within range) |
| NAV_ENTER | Open VALUE item for editing; or fire ACTION item | Commit the edit value to the item |
| NAV_BACK | Close the menu (cancel) | Discard the in-progress edit and return to BROWSE |
| NAV_NONE | No-op | No-op |

### Menu Result Output

After processing each navigation event, the menu returns a result struct with the following fields:

| Field | Type | Meaning |
|---|---|---|
| changed | boolean | True if visible state moved, indicating the caller should redraw |
| closed | boolean | True if menu just closed (NAV_BACK in BROWSE) |
| committedId | integer | The item ID of a committed VALUE (0 or positive means a value was committed) |
| actionId | integer | The item ID of a fired ACTION (0 or positive means an action triggered) |
| value | integer | The committed value (valid when committedId is present) |

### Item Types

| Kind | Description | Value range |
|---|---|---|
| VALUE | A scalar field the user scrolls (e.g., universe number, brightness) | Bounded by min/max with modular wrap |
| ACTION | A trigger (e.g., Exit, Save, Reboot) | No value; fires immediately on ENTER |

### Capacity Limits

| Limit | Value | Description |
|---|---|---|
| Maximum items | 8 | Hard limit; items beyond this are silently dropped |
| Label length | 11 characters | Truncated to 11 chars plus a null terminator |

## 3. State Machine

The menu is a two-mode state machine:

```
                Closed
                   |
              open()
                   |
                   v
    +-----------------------------+
    |           BROWSE            |
    |  (highlight navigation)     |
    +-----------------------------+
        |       |           |
    INC/DEC  ENTER VALUE   ENTER ACTION
        |       |           |
        |       v           v
        |  +-----------------------------+
        |  |            EDIT            |
        |  |  (value adjustment)        |
        |  +-----------------------------+
        |      |                |
       INC/   ENTER (commit)   BACK (discard)
       DEC      |                |
                v                |
          (commit to item,      |
           return to BROWSE) ----+
        |
    BACK (close)
        v
    Closed
```

### State Transitions

| From | Event | To | Side effect |
|---|---|---|---|
| BROWSE | NAV_INC | BROWSE | Highlight next enabled item; changed=true |
| BROWSE | NAV_DEC | BROWSE | Highlight previous enabled item; changed=true |
| BROWSE | NAV_ENTER on ACTION | BROWSE | actionId set to item ID; changed=true |
| BROWSE | NAV_ENTER on VALUE | EDIT | editValue set to item current value; changed=true |
| BROWSE | NAV_BACK | CLOSED | Close; closed=true; changed=true |
| EDIT | NAV_INC | EDIT | editValue = wrap(editValue+1, min, max); changed=true |
| EDIT | NAV_DEC | EDIT | editValue = wrap(editValue-1, min, max); changed=true |
| EDIT | NAV_ENTER | BROWSE | item.value = editValue; committedId set to item ID; value set to editValue; changed=true |
| EDIT | NAV_BACK | BROWSE | Discard editValue; changed=true |
| any | NAV_NONE | (same) | No-op; result is all-false |

Disabled items (enabled=false) are skipped during navigation and are not drawn.

## 4. Data Flow

1. **Menu construction**: The caller clears the menu, then adds VALUE items (with a label, initial value, and min/max range) and ACTION items (with a label). Universe VALUE items use the configured maximum universe as their upper bound.
2. **Menu open**: The caller calls open(), which enters BROWSE mode and highlights the first enabled item.
3. **Navigation event dequeue**: The caller polls the input mapping layer for a navigation event (NAV_INC, NAV_DEC, NAV_ENTER, NAV_BACK, or NAV_NONE).
4. **Dispatch**: The navigation event is passed to handle(). If the menu is CLOSED or the event is NAV_NONE, an all-false result is returned.
5. **BROWSE mode**: INC/DEC moves the highlight among enabled items with wraparound. ENTER on an ACTION item fires the action (setting actionId in the result). ENTER on a VALUE item enters EDIT mode, initializing the edit value to the item current committed value. BACK closes the menu.
6. **EDIT mode**: INC/DEC adjusts the edit value, wrapping within the items [min, max] range using modular arithmetic. ENTER commits: the edit value replaces the items value, and the result carries committedId and value. BACK discards the edit and returns to BROWSE without committing.
7. **Render**: The caller checks the changed flag in the result. If true, it redraws the menu on the display, showing item labels, the current highlight position, and (in EDIT mode) the working edit value.
8. **Commit handling**: If committedId is present, the caller reads the committed value and writes it into the corresponding configuration field. The caller then persists the change and may trigger a reboot if the changed field requires one.
9. **Action handling**: If actionId is present, the caller dispatches the action (e.g., Exit closes the menu, Save persists and optionally reboots, Reboot restarts the system).
10. **Menu close**: When the menu closes (via BACK or an Exit action), the caller returns to normal operation.

## 5. Configuration Integration

The menu engine reads no configuration fields directly. The caller provides all values at construction time:

| Configuration Source | Used for |
|---|---|
| Configured maximum universe | Upper bound for universe VALUE items |
| Current configuration values | Initial value for each VALUE item at construction |
| Field min/max ranges | Bounds for VALUE item editing |
| Live vs reboot metadata | The caller annotates each committed value with whether it applies instantly or requires a reboot, based on the field flags |

The caller maintains an ID-to-config-field mapping table, translating each item ID into the corresponding configuration field. When a VALUE is committed, the caller writes the value into the correct configuration field and applies the appropriate persistence and reboot semantics.

A first-boot wizard flow is supported: when non-volatile storage is empty on power-up, the system enters an interactive setup sequence that presents configuration fields one at a time through the menu, guiding the user through network selection, output configuration, and other initial settings before saving the complete configuration to persistent storage. The wizard uses the live-vs-reboot metadata to indicate to the user which settings take effect immediately and which require a restart.

## 6. Lifecycle

1. **Build**: The caller clears the menu and populates it with VALUE and ACTION items, typically in response to a user entering the configuration interface or on first-boot setup.
2. **Open**: The caller calls open(), entering BROWSE mode and highlighting the first enabled item.
3. **Run**: The caller polls for navigation events and dispatches each to handle(). After each dispatch, the caller checks the result and renders or persists as needed.
4. **Close**: The menu closes via NAV_BACK in BROWSE mode, or via an Exit ACTION item firing. The caller returns to normal operation.
5. **Destroy**: No teardown; the menu struct is stack/instance-local.

The first-boot wizard is a special lifecycle flow: on initial power-up with empty storage, the system bypasses normal operation and enters the wizard, which presents fields in a guided sequence, validates inputs, and saves the complete configuration before transitioning to normal operation.
## 7. Error Handling

| Condition | Behavior |
|---|---|
| Menu closed or NAV_NONE event | Returns an all-false result (no change, no commit, no action, no close) |
| Too many items (beyond 8-item cap) | Additional items are silently dropped during construction; no error is reported to the caller |
| Label too long (beyond 11 characters) | Label is truncated to 11 characters plus a null terminator |
| Single enabled item | Navigation INC/DEC wraps to the same item; no error |
| Invalid value range (min greater than max) | The wrap function falls back to returning the minimum value; no error |
| NAV_BACK in EDIT mode | Discards the in-progress edit silently; no confirmation prompt is shown |
| Committed value out of range | The menu engine only produces values within [min, max] through modular wrapping; the caller is responsible for range validation against the target configuration field constraints before applying |

No error logging, no error codes, and no exception paths exist in the menu engine. The silent truncation at the 8-item cap is the only data-loss scenario; all other conditions produce defined behavior without loss of information.

## 8. Timing Constraints

| Constraint | Value | Rationale |
|---|---|---|
| Event processing | Sub-microsecond | Pure integer math, no I/O; completes well within a single loop iteration |
| Render latency | After next loop iteration | handle() is called once per dequeued event; rendering follows immediately in the loop |
| Debounce/long-press | 25 ms / 600 ms | Governed by the input mapping layer, not the menu engine itself; nav events arrive already classified |
| Menu build | Synchronous | Called once per menu open; at most 8 items |
| No hard deadline | - | The menu runs at human-interaction speed; it does not interact with the 1 ms DMX transmit tick |

## 9. Memory and Allocation Model

- The item array is a fixed-capacity static allocation (8 items maximum).
- Each item label is a fixed 12-byte buffer (11 chars plus null terminator).
- The result struct is returned by value (no heap allocation).
- No dynamic allocation occurs. The entire menu (8 items with labels) fits in approximately 200 bytes of stack/instance memory.

## 10. Safety Considerations

- **Bounded capacity**: The 8-item limit with silent truncation prevents unbounded memory growth. Callers designing menus with more than 8 fields must split them across sub-menus.
- **Range enforcement**: VALUE items are bounded by min/max with modular wrapping, preventing the user from setting values outside the valid range for any field.
- **Commit-before-persist**: The menu never writes to persistent storage directly. It reports committed values to the caller, which applies persistence. This allows the caller to batch saves or defer writes.
- **Edit cancellation**: NAV_BACK in EDIT mode discards the in-progress edit without committing, preventing accidental partial changes from being persisted.
- **Disabled item safety**: Disabled items are skipped by navigation and not drawn, preventing the user from interacting with unavailable options.
- **Live vs reboot distinction**: The caller annotates each field with whether it applies instantly or requires a reboot. This prevents the user from assuming a change is active when it is pending a restart.

## 11. Cross-Module Dependencies

| Module | Provides to Menu | Consumes from Menu |
|---|---|---|
| Input Mapping | Navigation events | - |
| Config Engine | Configuration descriptor tables (field keys, ranges, current values) | Committed values for persistence |
| Display System | - | Menu result (labels, highlight, edit value) for rendering |
| Non-Volatile Storage | - | Committed values for persistence |
| WiFi Manager | - | Reboot/network restart trigger (via action callbacks) |
| Main Loop | Navigation events from physical or serial input | Menu state and render instructions |

## 12. Testing Verification

- **No dedicated test coverage**: The menu engine has no native host or Unity test. The Unity test build filter does not include the menu module sources.
- **Design intent for host testability**: The module is header-only with no external dependencies beyond standard integer types, designed to be testable in isolation.
- **Manual verification path**: Intended to be verified on hardware through the physical encoder/button interface or via serial console navigation.

## 13. Open Questions

1. Whether the Menu instance is intended to be a global singleton or a local variable in the main loop.
2. What display rendering function consumes the MenuResult.
3. How the item ID maps back to a specific configuration field the caller must maintain the mapping table.
4. Whether the menu supports sub-menus drill-down actions that open a child menu.
5. Whether the first-boot wizard flow is driven by the menu engine or by a separate setup sequence.
6. Whether NAV_BACK discard in EDIT mode will gain a confirmation prompt for destructive actions.

## 14. History

- The menu engine was created as the top layer of a three-tier input stack (raw decode to physical mapping to menu), designed to be a tiny, generic, display-driven menu with no hardware or configuration dependencies, enabling host testability.
- The two-mode design (BROWSE for navigation, EDIT for value adjustment) was chosen to separate cursor movement from value editing, matching standard embedded UI conventions.
- Disabled-item skipping was implemented so that fields unavailable on a particular hardware configuration simply vanish from the menu rather than appearing as greyed-out entries.
- The Exit ACTION item invariant was established so that a unit with only a single button (which cannot produce a BACK event) can still leave the menu via ENTER on the Exit action.
- The 8-item capacity limit and 11-character label length were chosen to balance usability with the memory constraints of embedded display systems.
- Long-press synthesis in the input mapping layer allows sparse button sets to reach all four navigation primitives, complementing the menu single-button-Exit guarantee.
