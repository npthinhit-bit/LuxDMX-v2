# Config Engine Specification

Domain: cfg.config-engine

## 1. Module Overview

The Config Engine is the single source of truth for the firmware's runtime configuration. It resolves, persists, and exposes every user-editable setting through one descriptor table that drives three surfaces simultaneously: Non-Volatile Storage (NVS) persistence, the web configuration form, and the serial console command grammar.

The engine owns a flat `Config` struct (47 scalar and string fields) and a per-output `DmxOutput` struct (24 fields per output, up to 4 outputs). Each field is described by a `CfgField` or `CfgOutputField` descriptor that records the field key, JSON key, data type, min/max range, human-readable label, UI group, and a set of behavioral flags. The field table is the only place where defaults, constraints, and metadata live; no scattered `DEF_*` macros exist.

The resolution order at boot is: neutral constraint value -> active board template -> saved NVS value. Board templates are selected at build time per target and support recursive inheritance via an `extends=` directive (capped at 8 levels of nesting).

## 2. External Interfaces

### Caller-Facing API

| Function | Visibility | Purpose |
|---|---|---|
| `load` | Public, called from setup phase 2 | Applies neutral values, then the board template, then overlays NVS-saved values |
| `save` | Public, called from setup, web routes, artnet_bridge, net_state | Writes all global and per-output fields to NVS |
| `setValue` | Public, called from web routes and serial console | Sets one field by key; clamps to [min, max]; returns error on unknown key |
| `getValue` | Public, called from web routes | Reads one field by key |
| `dump` | Public, called from serial console | Prints every field as `key=value` |
| `resetTo` | Public, called from web routes | Resets config to a named template then caller persists |
| `importJson` | Public, called from web `/config` POST | Line-based JSON key/value scanner; validates all keys recognized |
| `exportJson` | Public, called from web `/config` GET and `/info.json` | Emits all non-secret fields as a JSON object |
| `importXml` | Public | Token-based XML import; validates all keys recognized |
| `exportXml` | Public | Emits all fields as structured XML |
| `applyTemplateText` | Public | Parses and applies a template string with `extends=` inheritance |
| `migrateNvsKeys` | Public, called from setup phase 1 | One-shot migration of legacy NVS keys |

### Serial Console API

A line-oriented text parser accepts these verbs (one per line, newline-terminated):

| Verb | Description |
|---|---|
| `dump` | Prints `key=value` for every field |
| `key=value [...]` | Sets one or more fields (space/comma-separated tokens) |
| `get <key>` | Prints a single field value |
| `set <key> <value>` | Sets one field; prints `OK` or `ERR <msg>` |
| `save [reboot]` | Persists to NVS; reboots if `reboot` is given |
| `wifi <ssid> [pass]` | Triggers the WiFi hook callback |
| `reboot` | Triggers the reboot hook callback |
| `factory` | Triggers the factory-reset hook callback |
| `help` / `?` | Prints help text |

### NVS Key Namespace

All persisted settings live in the `"dmxgw"` NVS namespace using two key schemes:

| Scope | Key Format | Example |
|---|---|---|
| Global fields | `f.key` | `hostname`, `ledtype` |
| Output A | `a_<suffix>` | `a_universe`, `a_mergeMode` |
| Output B | `b_<suffix>` | `b_universe` |
| Output C | `c_<suffix>` | `c_universe` |
| Output D | `d_<suffix>` | `d_universe` |
| Legacy output 0 | `o0_<suffix>` (migrated) | Migrated to `a_*` |
| Legacy output 1 | `o1_<suffix>` (migrated) | Migrated to `b_*` |
| Legacy link-loss | `apfb` (migrated) | Migrated to `fbmode` |

Scene-specific keys live in a separate `"scenes"` namespace with the `scn_s{idx}*` naming scheme.

## 3. State Machine

The Config Engine has no persistent state machine. Each API call is a pure function of the current in-memory `Config` struct and the NVS key space.

The serial console parser internally implements a single-pass line tokenizer: it reads bytes until a newline (capped at 600 characters per line), splits tokens by whitespace or comma, splits each token on `=`, and dispatches to a verb handler. Invalid commands, unknown keys, and out-of-range values produce error strings rather than state transitions.

The boot-time resolution (`load`) follows a linear sequence: neutral fill -> template application -> NVS overlay. There is no rollback on partial failure; invalid NVS values are clamped to range and accepted.

## 4. Data Flow

1. **Boot (setup phase 1):** `migrateNvsKeys("dmxgw")` scans the NVS namespace. It renames legacy output keys (`o0_*` -> `a_*`, `o1_*` -> `b_*`) for outputs 0 and 1 only, maps the boolean `apfb` key to the `fbmode` enum, and namespaces scene keys (`s{idx}*` -> `scn_s{idx}*`). The migration is idempotent and a no-op after the first successful run.

2. **Boot (setup phase 2):** `load()` resets all fields to neutral defaults, applies the active board template (resolving `extends=_base` inheritance up to 8 levels deep), then overlays NVS-saved values for each field in the descriptor table. Integer values from NVS are clamped to `[min, max]`.

3. **Runtime mutation (serial):** Each `loop()` cycle, the serial parser drains incoming bytes, buffers them into a line buffer, and on newline dispatches to the command executor. The executor calls `setValue` for each `key=value` token, returning `"OK"` or `"ERR <msg>"`.

4. **Runtime mutation (web):** The web `/config` POST handler calls `importJson` to scan the request body for `"key":"value"` or `"key":value pairs. Recognized keys are applied via `setValue`; unrecognized keys cause the entire import to fail with an error. If any Art-Net config field was modified, a dirty flag is set.

5. **Persistence (lazy save):** A dirty flag checked each `loop()` cycle triggers `save()`, which iterates both descriptor tables and writes each field to NVS. Integer booleans and strings are all persisted per-field.

6. **Template change (web):** The web `/config` template-switch handler calls `resetTo(name)`, which re-applies neutral values then the named template. The caller then persists via `save()` and may trigger a reboot if any `CFG_REBOOT` fields changed.

## 5. Configuration Integration

This module **is** the configuration integration layer. Every `Config` and `DmxOutput` field is described by a row in the schema table.

### Field Flags

| Flag | Semantics | Affected fields (examples) |
|---|---|---|
| `CFG_REBOOT` (default) | Requires a firmware restart to take effect | LED pin/type, DMX TX/RX/RTS pins, UART port, display config, network mode, WiFi SSID, static IP, VLAN, syslog port |
| `CFG_LIVE` | Applies instantly without restart | Universe, net, subnet, sACN universe/sync, merge mode, loss mode, loss preset, failsafe timeout, TX rate/style/source, break/MAB, invert, input mode, split mask, brightness, timecode, syslog, webhook, Art-Net RDM |
| `CFG_SECRET` | Masked as `"***"` in dumps and JSON exports | OTA password, WiFi PSK, AP password, webhook URL |
| `CFG_NOWEB` | Excluded from the web `/config` form | Auto-update toggle |
| `CFG_KEEPNE` | Blank web form values are ignored (not cleared) on import | Hostname, OTA password, WiFi PSK |
| `CFG_READONLY` | Not writable at all | (none currently assigned) |

### Field Metadata

Each field descriptor records: a unique key string, a JSON export key, a type kind (integer, boolean, string, enum), a min/max range, a human-readable label, a UI group name, behavioral flags, and optional enumerated value labels. Integer fields are always clamped to `[min, max]` on set and during NVS load. No accessor methods exist on the structs; all field access is direct (e.g., `cfg.hostname`).

## 6. Lifecycle

1. **Compile time:** A build hook invokes a template generator that reads the board template files and embeds them as read-only constant strings. Each template inherits from a base template via the `extends=` directive and is selected per build via a build-flag macro.

2. **NVS migration (setup phase 1):** `migrateNvsKeys` performs one-shot key renaming in the `"dmxgw"` namespace. Legacy output keys for outputs 0 and 1 are renamed; the boolean `apfb` link-loss policy is migrated to the multi-policy `fbmode` enum; scene keys are namespaced. This is idempotent and a no-op on subsequent boots.

3. **Config load (setup phase 2):** The neutral constraint values are applied, the active board template is resolved (with inheritance), and NVS-saved values overlay on top. Per-output pin and GPIO fields are validated by the output init module's sanitize pass.

4. **Serial console begin (setup phase 5):** The console is initialized with empty hook callbacks (no save/reboot/factory/wifi callbacks registered at init time).

5. **Main loop (runtime):** Each loop iteration, the serial parser drains one character. A dirty flag checked each loop triggers persistence via `save()`. Network protocol modules also trigger `save()` on their own state changes.

## 7. Error Handling

| Operation | Failure mode | Behavior |
|---|---|---|
| `setValue` | Unknown key | Returns error code; logs `"unknown key: <key>"`; sets error string |
| `getValue` | Unknown key | Returns error code; logs `"key not found: <key>"` |
| `applyTemplateText` | Nesting depth exceeds 8 | Returns error state; template not applied |
| `applyTemplateText` | A `setValue` inside the template fails | Returns error; partial field updates are applied |
| `applyNamed` | Unknown template name | Returns not-found error; logs the name |
| `importJson` | Unrecognized key in import | Returns error code; logs `"importJson: some keys not recognized"`; no fields are persisted |
| `importXml` | Unrecognized key in import | Returns error code; no fields persisted |
| `dump` / `exportJson` | Secret field | Value is replaced with `"***"` |
| NVS `load` / `save` | Flash corruption or NVS failure | Treated silently as "use neutral/template value"; `Preferences.getInt` returns 0 on failure, which the engine interprets as the range minimum |
| Serial console | Unknown verb | Returns `"ERR unknown command: <verb> (try 'help')"` |
| Serial console | Line exceeds 600 characters | Input is silently truncated at the cap |

Integer values are always clamped to `[min, max]` via a constrain function in both the write path and the NVS load path. Template lines exceeding 192 bytes are silently truncated during parsing.

## 8. Timing Constraints

- **Serial parser:** Non-blocking. Drains at most one character per `available()` iteration per `loop()` cycle. The 600-character input cap bounds worst-case accumulation.
- **Config save:** Iterates over all global fields plus `MAX_OUTPUTS` x per-output fields. Total operation count is bounded and small (~158 NVS key operations for the full config). Triggered lazily via dirty flags, not on every field change.
- **NVS migration:** One-shot at boot, executed once before the device is considered initialized. Not a recurring cost.
- **No hard deadline** applies to the Config Engine itself; all operations complete within a single `loop()` iteration on core 0.

## 9. Memory & Allocation Model

All data structures are static:

- The `Config` singleton is a static global struct. No heap allocation is used for config values themselves; individual `String` fields may grow on the heap as needed, but the struct layout is fixed.
- The descriptor tables (`CONFIG_FIELDS[]` and `OUTPUT_FIELDS[]`) are `static const` arrays in read-only data.
- The template strings are embedded as read-only constant arrays at compile time, generated from the board template files. They reside in memory-mapped flash and are directly readable without PSRAM or heap allocation.
- The serial console line buffer is capped at 600 characters and is a static file-scope allocation.
- NVS handles are opened and closed per call in `load()` and `save()` — no persistent handle is held.
- No dynamic allocation occurs in the config engine path itself.

## 10. Safety Considerations

- **No concurrent mutation:** The config struct is not concurrently mutated from core 1. All `load()`, `save()`, `setValue`, and serial-console operations occur on core 0 within `loop()` or setup. No locking is used.
- **Range clamping on load:** NVS values are clamped to the descriptor-defined `[min, max]` range during the overlay step, preventing out-of-spec values from hardware-damaging peripherals (e.g., a corrupted pin number or UART port).
- **Template nesting cap:** Recursive `extends=` inheritance is capped at 8 levels, preventing infinite recursion or stack exhaustion from malformed template files.
- **One-shot migration:** NVS key migration runs exactly once and is idempotent; a partially-completed migration does not corrupt subsequent boots because already-migrated keys are skipped.
- **Secret masking:** Sensitive fields (passwords, webhook URLs) are never exposed in dumps or JSON exports, preventing accidental disclosure over the serial console or a web API response.

## 11. Cross-Module Dependencies

| Module | Provides to Config Engine | Consumes from Config Engine |
|---|---|---|
| Main entry (setup) | — | Calls `load` and `migrateNvsKeys` at setup phases 1 and 2 |
| Output Init | — | Reads `cfg.outputs[]` for per-output pin mapping and sanitization |
| LED Status | — | Reads `cfg.ledType`, `cfg.ledPin`, `cfg.ledBrR[]` |
| Syslog | — | Reads `cfg.syslogEnabled`, `cfg.syslogServer`, `cfg.syslogPort` |
| Art-Net Protocol | — | Reads `cfg.*` fields; writes dirty flag triggering `save` |
| sACN Protocol | — | Reads `cfg.*` fields |
| Web Server / Routes | — | Calls `exportJson`, `importJson`, `setValue`, `save` |
| Serial Console | Hooks (save/reboot/factory/wifi callbacks) | Calls `setValue`, `getValue`, `dump` |
| Ethernet | — | Reads network mode, pins, VLAN, static IP |
| Task Scheduler | — | Reads `cfg.outputs[i].txRate`, `txStyle`, `enabled` |
| Scene Engine | — | Reads `cfg.outputs[o].enabled` |
| Rate Limiter | — | Reads rate-limit-related config fields |

The Config Engine owns the `"dmxgw"` NVS namespace. No other module reads or writes application settings to NVS.

## 12. Testing Verification

| Test | Scope |
|---|---|
| `config_test` (native host) | Template resolution, single-field set/get round-trip, NVS save/load round-trip, serial console grammar (all verbs), secret masking |
| `test_unit_config` (Unity) | 8 tests: template defaults, set/get, NVS round-trip, board template differences (RDM-full vs DMX-only outputs), all serial commands including invalid input |
| `seqlock_test` | Validates the generic seqlock primitive (used upstream by the DMX buffer, not this module) |
| No NVS migration tests | Legacy key migration path is not covered by the test suite; the native test uses an empty NVS shim with no legacy keys |
| No template inheritance unit tests | The `extends=` recursive resolution is exercised only through template-resolution smoke tests |

## 13. Open Questions

1. Whether the `CFG_KEEPNE` flag is honored during NVS `load()`. The engine applies NVS values unconditionally during the overlay; the "keep if blank" logic may live in the web route handler rather than in the engine itself.
2. Whether the `Preferences` shim used by native tests persists across `resetToTemplate` calls between test cases, given that tests use `Preferences::clearAll()` in setup.
3. Whether `importJson` handles the `outputs` array — the parser scans for flat `"key":"value"` pairs but `outputs` is a nested structure; it may not recurse into arrays.
4. Where the serial console hook callbacks (save/reboot/factory/wifi) are registered, given that `begin` is called with empty hooks at setup phase 5.

## 14. History

- The `Config` and `DmxOutput` structs were moved out of the main entry file into a shared header so they are includable by all five layers.
- The NVS key scheme changed from per-output `o0_*`/`o1_*` (2-output builds) to letter-prefixed `a_*`/`b_*`/`c_*`/`d_*` (4-output builds), with a one-shot migration for outputs 0 and 1.
- The `apfb` boolean link-loss policy was migrated to the `fbmode` enum to support four fallback policies: wired retry, access point, reboot, and WiFi fallback.
- Scene NVS keys were namespaced from `s{idx}*` to `scn_s{idx}*` to avoid future key collisions in the shared namespace.
- Template-based config defaults replaced scattered `DEF_*` macros; templates are now data files resolved at build time and embedded as read-only constants.
- JSON import/export was added to support the web configuration POST endpoint, using a line-based scanner rather than a full JSON parser for footprint reasons.
- The `DEFAULT_TEMPLATE` selection moved from a preprocessor macro to a per-environment build flag, allowing each board target to specify its own base template.
