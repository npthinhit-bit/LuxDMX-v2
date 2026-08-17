# Input Mapping Specification

Domain: app.input-map

## 1. Module Overview

The Input Mapping module provides the unified interface for all external user inputs to the firmware. It encompasses two independent input paths that both normalize physical or text-based interactions into a small alphabet of abstract events:

1. **Serial console input** — a text-based command channel operating at 115,200 baud that accepts configuration commands, status queries, and system control verbs. It provides line editing, command history recall, and structured response formatting.
2. **Physical controls input** — a quadrature encoder and up to four buttons (or a single button without an encoder) that produce navigation events for an on-display menu system.

The module owns the command grammar (the set of recognized verbs and their argument syntax), the line-editing buffer with backspace and re-entry support, the command history ring, the navigation-event vocabulary (NAV_NONE, NAV_INC, NAV_DEC, NAV_ENTER, NAV_BACK), and the response formatter that emits structured text to the serial port.

It delegates raw character reception to the underlying serial transport and raw GPIO level sampling to the caller poll loop. It does not perform hardware I/O directly — the caller reads GPIO levels and feeds them in, or reads serial bytes and passes complete lines.

It is consumed by the interactive menu system (which receives navigation events) and by the firmware main event loop (which polls serial input and dispatches commands).
## 2. External Interfaces

### Serial Console Interface

The serial console is a half-duplex, text-mode channel at 115,200 baud (8-N-1). It accepts newline-terminated lines and emits structured text responses. Input is character-by-character with local echo and line editing.

**Command Grammar**

Each command is a single newline-terminated line, parsed by whitespace and comma delimiters. Arguments may use key=value assignment syntax within a line.

| Verb | Form | Description |
|---|---|---|
| dump | dump | Prints every configuration field as key=value, one per line |
| get | get key | Prints a single field as key=value |
| set | set key value | Sets one field; prints OK or ERR message |
| save | save [reboot] | Persists all current settings to non-volatile storage; reboots if reboot argument given |
| list | list [key] | Lists all field keys, or all keys matching a filter |
| load | load | Reloads configuration from non-volatile storage into the active runtime set |
| restart / reboot | reboot | Triggers a system reboot |
| wifi | wifi ssid [pass] | Triggers WiFi credential update hook |
| factory | factory | Triggers a factory-reset hook (erases all saved settings) |
| help / ? | help | Prints the help text listing all available verbs |
| key=value | key=value [key2=val2 ...] | Sets one or more fields inline (space or comma separated); prints OK or ERR message per field |

**Response Formatting**

All responses are plain text terminated by a newline. Success responses are OK. Error responses are ERR descriptive-message. Query responses are key=value. Dump output is key=value lines with secret fields masked as key=***.

### Physical Controls Interface

The physical input interface consists of an optional quadrature encoder (with push-button) and up to four independent momentary buttons. These produce navigation events consumed by the menu system.

| Input | Produces |
|---|---|
| Encoder rotate clockwise | NAV_INC |
| Encoder rotate counter-clockwise | NAV_DEC |
| Encoder push (short press) | NAV_ENTER (or NAV_INC in solo mode) |
| Encoder push (long press) | NAV_BACK (or NAV_ENTER in solo mode) |
| Button 1 (short) | NAV_INC |
| Button 1 (long) | NAV_ENTER |
| Button 2 (short) | NAV_DEC |
| Button 2 (long) | NAV_ENTER |
| Button 3 (short) | NAV_ENTER |
| Button 3 (long) | NAV_BACK |
| Button 4 (short) | NAV_BACK |
| Single button, no encoder | Short to NAV_INC, Long to NAV_ENTER |

### Input Event Vocabulary

| Event | Meaning |
|---|---|
| NAV_NONE | No event (no-op) |
| NAV_INC | Increment / next / scroll up |
| NAV_DEC | Decrement / previous / scroll down |
| NAV_ENTER | Select / drill in / confirm |
| NAV_BACK | Cancel / leave |

## 3. State Machine

The serial console parser implements a per-line state machine:

| Phase | Behavior |
|---|---|
| Receiving | Characters are accumulated into a line buffer; backspace deletes the last character; carriage return and newline trigger execution |
| Execute | The accumulated line is tokenized and dispatched to a verb handler |
| Respond | The handler emits a text response and control returns to Receiving |

The physical input path has no top-level state machine; it is polling-based. Three independent sub-trackers operate within each poll cycle:

| Sub-tracker | States |
|---|---|
| Encoder detent | Seeded to Tracking to No-edge |
| Button (encoder push and each of 4 buttons) | Idle to Debounce-window to Pressed to Released |

A long-press threshold synthesizes missing navigation primitives when the physical input set is sparse (e.g., a single button can reach all four nav events through short/long distinction).
## 4. Data Flow

### Serial Console Path

1. **Character reception**: The firmware main loop reads one byte at a time from the serial port. Characters are echoed back to the terminal and appended to an in-memory line buffer (capped at a maximum length).
2. **Line editing**: Backspace characters remove the last buffered character. Tab, arrow keys, and other control characters are either handled (cursor movement, history recall) or ignored.
3. **Command history**: When Enter is received, the completed line is stored in a circular history buffer. The up/down arrow keys cycle through prior entries, allowing recall and re-editing.
4. **Newline trigger**: A carriage-return or newline character terminates the current line, triggering command execution.
5. **Tokenization**: The line is split into tokens by whitespace and commas. Each token is further split on = to extract key/value pairs.
6. **Verb dispatch**: The first token is matched against the verb table. Remaining tokens are parsed as arguments.
7. **Field resolution**: For set/get/key=value forms, the key is looked up in the configuration descriptor table. Unknown keys produce an error.
8. **Value validation**: Integer values are clamped to the field declared minimum/maximum range. Enum values are validated against known labels. String values are length-bounded.
9. **Response emission**: The result (OK, ERR message, or key=value) is written to the serial port.

### Physical Controls Path

1. **GPIO sampling**: The firmware poll loop reads raw voltage levels from the encoder A/B pins, the encoder push-button pin, and up to four button pins, assembling a snapshot struct.
2. **Rotation decode**: If the encoder is present, the A/B transitions are decoded through a quadrature state table, accumulating sub-detent edges until a full mechanical detent is reached (emitting +1 or -1).
3. **Button debounce**: Each button level is passed through an edge debouncer (25 ms floor). Press and release edges are detected and timestamped.
4. **Press classification**: On a debounced release edge, the hold duration is classified as either a short press or a long press (600 ms threshold).
5. **Role-to-event mapping**: Each button has a configured role (Enter, Back, Next, Prev) that, combined with the press classification (short/long), maps to a navigation event. In solo mode (single button, no encoder), short maps to INC and long maps to ENTER.
6. **Event queueing**: Navigation events are placed into a small lossless ring buffer (8 entries).
7. **Menu consumption**: The menu system drains events one at a time from the queue and processes each as a single input to its state machine.

## 5. Configuration Integration

### Serial Console Settings

| Setting | Default | Range | Applies |
|---|---|---|---|
| Baud rate | 115,200 | Standard rates | At boot (reboot) |
| Line buffer length | 600 characters | - | Per-line execution |
| History depth | 8 entries | - | Interactive recall |

### Physical Control Settings

| Setting | Default | CFG flag |
|---|---|---|
| Encoder present | No (pins at neutral) | Reboot |
| Encoder steps per detent | 4 (standard EC11) | Live |
| Encoder direction reversed | No | Live |
| Button 1 role | Next (+, INC) | Live |
| Button 2 role | Prev (-, DEC) | Live |
| Button 3 role | Enter (ENT/ENTER) | Live |
| Button 4 role | Back | Live |
| Button active-high | No (active-low, pull-up) | Live |
| Long-press threshold | 600 ms | Live |
| Debounce floor | 25 ms | Live |

Button role values are: off (ignored), Enter/Select, Back, Next, Prev. The default assignments implement a four-way navigation pad. With only one button and no encoder, the system enters solo mode where short=increment and long=enter, allowing full menu traversal.

## 6. Lifecycle

1. **Boot (setup)**: The serial port is initialized at 115,200 baud. The command history buffer is cleared. The line-editing buffer is reset. The physical input decoder is initialized from configuration if present.
2. **Main loop (runtime)**: Each loop iteration, the input mapper polls the serial port for new characters (processing one character per iteration) and, if active, samples GPIO levels and feeds them to the physical input decoder.
3. **Command execution**: When a complete line is received, the parser dispatches the verb and emits a response.
4. **History recall**: Arrow-up/arrow-down keys recall prior commands from the history ring for editing and re-submission.
5. **Shutdown**: No explicit teardown. The serial port is simply abandoned on reboot.

## 7. Error Handling

| Condition | Behavior |
|---|---|
| Unknown command verb | Response: ERR unknown command: verb (try help) |
| Unknown configuration key | Response: ERR unknown key: key |
| Value out of range | Response: ERR message; value is clamped to declared min/max |
| Invalid enum value | Response: ERR message; no field is modified |
| Line exceeds maximum length | Input is silently truncated at the cap; execution proceeds with the truncated line |
| Secret field in dump | Value is masked as *** |
| Queue full (physical input) | The newest navigation event is silently dropped; the design capacity (8 entries at 1 kHz polling) is sufficient for human input |
| Queue empty (physical input) | Returns NAV_NONE (no-op) |
| WiFi hook not available | Response: ERR wifi not available |
| Save hook not available | Response: ERR save not available or OK saved depending on initialization state |

## 8. Timing Constraints

| Constraint | Value | Rationale |
|---|---|---|
| Serial baud rate | 115,200 | Standard ESP32 default; all interactive sessions use this rate |
| Character polling | One per loop iteration | Non-blocking; a full line of 600 chars takes up to 600 loop cycles to receive |
| Physical input poll rate | ~1 kHz (1 ms target) | Human input cannot outrun this rate; debounce and long-press thresholds are human-scale |
| Debounce floor | 25 ms | Mechanical contact noise below this duration is ignored |
| Long-press threshold | 600 ms | Standard hold-to-distinguish from a tap |
| Queue capacity | 8 events | Sufficient for 1000 polls/sec of human input; overflow is practically impossible |
| No hard real-time deadlines | - | Input processing is best-effort within the firmware loop; it does not affect the 1 ms DMX transmit tick |
## 9. Memory and Allocation Model

- The serial line buffer is a fixed-capacity static allocation (600 characters maximum).
- The command history ring is a fixed array of 8 string slots.
- The physical input event queue is a fixed 8-entry ring buffer (one byte per entry).
- The physical input decoder state (quadrature accumulator, button debouncers) is embedded in a single instance struct.
- No heap allocation occurs in the input path. No PSRAM involvement.
- Total static footprint is approximately 640 bytes (line buffer + 8 history slots + event queue + decoder state).

## 10. Safety Considerations

- **Input isolation**: Serial console input is processed on the network-receiving core only; it never interrupts the DMX transmit core. Physical input polling is sub-microsecond per cycle and runs alongside the serial console in the main loop.
- **Range clamping**: All configuration values set via serial commands are clamped to their declared minimum/maximum before application, preventing out-of-spec values from reaching hardware drivers.
- **Secret masking**: Sensitive fields (passwords, keys) are never displayed in full during a dump — they are masked as ***, preventing accidental disclosure.
- **Buffer bounds**: The line buffer is length-capped; overlong input is truncated rather than overflowing memory.
- **No silent corruption**: Invalid keys produce an explicit error response rather than being silently accepted or partially applied.
- **Queue overflow awareness**: If the physical input queue overflows, the event is dropped silently. The design margin (8 entries at 1 kHz polling for human input) makes this practically impossible, but a caller cannot detect the loss.

## 11. Cross-Module Dependencies

| Module | Provides to Input Mapping | Consumes from Input Mapping |
|---|---|---|
| Config Engine | Configuration descriptor tables (field keys, types, ranges) | Field metadata for validation and listing |
| Menu System | Navigation events (NAV_INC/DEC/ENTER/BACK) | - |
| Main Loop | Serial byte stream, GPIO samples | Dispatched commands, navigation events |
| WiFi Manager | WiFi credential update hook | WiFi command callback |
| System Reboot | Reboot trigger | Reboot command callback |
| Factory Reset | Factory reset trigger | Factory reset command callback |
| NVS Persistence | Save trigger | Save command callback |

## 12. Testing Verification

- **Native config test**: Exercises the serial console grammar via the command executor — tests dump, get, set, and invalid commands against the configuration descriptor tables. Verifies secret masking and response formatting.
- **Unity config tests**: 8 tests covering all serial verbs including invalid input, value range clamping, and NVS round-trip after save/load.
- **No dedicated input-mapping test**: The physical input decoder (encoder quadrature, button debounce, long-press synthesis) has no host-native or Unity test coverage. The decoder is designed for host testability but no test file exercises it.

## 13. Open Questions

1. Whether the serial console supports in-line editing with cursor left/right movement, insert mode, or line recall beyond simple up/down arrow history.
2. Whether the load command supports loading from a named template (in addition to reloading from NVS), allowing the serial console to switch board profiles interactively.
3. Whether the command history persists across reboots or is purely in-RAM for the current session.
4. Whether tab completion for field keys is implemented in the serial console line editor.
5. Where the WiFi, save, reboot, and factory-reset hook callbacks are actually registered — the serial console is initialized with empty hooks, and the registration site is not visible in the input-mapping layer.

## 14. History

- The serial console command grammar was consolidated into a single descriptor-table-driven parser to unify the web form, serial interface, and NVS persistence into one field table.
- The key=value inline assignment form was added to allow setting multiple fields in a single line, alongside the explicit set verb form.
- The list command was added as an alias for dump (field listing) to provide a more intuitive verb for users scanning available configuration options.
- The load command was added to reload configuration from non-volatile storage without a reboot, as the inverse of save.
- Long-press synthesis was introduced so that a sparse button set (fewer than four buttons, or a single button with no encoder) can still reach all four navigation primitives, making the menu system usable on minimal hardware.
- The line buffer cap was set to 600 characters, sufficient for setting multiple fields in one command line.
- The command history ring was added to allow recall and editing of prior commands, reducing repetitive typing during configuration sessions.
