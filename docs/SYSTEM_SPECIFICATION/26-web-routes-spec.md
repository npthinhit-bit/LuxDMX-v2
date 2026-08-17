# Web Routes — System Specification

Domain: net.web-routes

## 1. Module Overview

The Web Routes module implements all dynamic HTTP handlers registered onto the core-0 AsyncWebServer (port 80). It is the sole producer of JSON API responses, schema-driven configuration forms, OTA initiation, RDM action triggers, network provisioning, and device diagnostics. Every handler emits structured JSON (or structured text for lightweight endpoints) and consumes or mutates the shared configuration state managed by the Config Engine.

The module is the bridge between the browser frontend and the firmware subsystems: it reads merged config/export state for JSON snapshots, writes config fields from POSTed form data, and stages RDM operations for the RDM engine. Rate limiting is applied at the route-registration layer (in the Web Server module) for config-save and OTA-initiation endpoints, using separate token buckets for config and OTA traffic.

**Owns:** All dynamic route handlers (JSON, config, OTA, RDM, setup, reset, health, diagnostics).
**Consumes:** Config Engine (field tables, export/import, save), RDM Engine (TOD, scan, operations), OTA module (GitHub/URL/local-task initiation), Stats module (health output), Soak Monitor (diagnostics export), Output Init (RDM line selection).
**Produces:** JSON responses, HTTP redirects (302 to /ota progress page), HTTP status codes.

## 2. External Interfaces

### 2.1 Registered HTTP Routes

### 2.2 JSON Snapshot Handlers

| Route | Method | Handler | Output |
|---|---|---|---|
| /info | GET | handleInfoJson | Device metadata: firmware version, board/MCU, config export, network state, uptime, heap, RSSI |
| /version | GET | handleVersionJson | version, build, variant, latestVersion, updateAvailable, otaProgress |
| /dmx.json | GET | handleDmxJson | 512-channel DMX snapshot for the monitored output |
| /senders.json | GET | handleSendersJson | Active network senders (IP, protocol, fps, last seen) |
| /log.json | GET | handleLogJson | Change log entries (currently returns empty array) |
| /rdm.json | GET | handleRdmJson | RDM status: enabled flag, line count, sent/recv counters, fixtures count, per-output universe mapping |

### 2.3 Config Routes

| Route | Method | Handler | Rate Limited | Output |
|---|---|---|---|---|
| /config | GET | handleConfigGet | No | Schema-driven HTML config form |
| /config | POST | handleConfigPost | Yes (config) | JSON: reboot flag, changed fields |
| /config/export | GET | handleConfigExport | No | Full config JSON (credentials redactable via include_credentials) |
| /config/import | POST | handleConfigImport | Yes (config) | Text: import success / HTTP 400 on failure |

### 2.4 Health Route

| Route | Method | Handler | Output |
|---|---|---|---|
| /health | GET | handleHealth | status, uptime, free heap, per-output signal status, network link metrics |

### 2.5 RDM Action Routes

| Route | Method | Handler | Action |
|---|---|---|---|
| /rdm/discover | GET | handleRdmTrigger | Trigger RDM device discovery (8-second budget) |
| /rdm/setaddr | GET | handleRdmTrigger | Set target device DMX start address |
| /rdm/identify | GET | handleRdmTrigger | Toggle RDM identify mode on target device |
| /rdm/setpers | GET | handleRdmTrigger | Set active personality index on target device |
| /rdm/setlabel | GET | handleRdmTrigger | Set device label string on target device |
| /rdm/tod | GET | handleRdmTod | Return Table of Devices (UID list and count) |
| /rdm/bqp | GET | handleRdmBqp | Get/set background-queue policy severity (0-4) |
| /rdm/merge | GET | handleRdmMerge | Get/set per-output merge mode |

### 2.6 OTA Routes

| Route | Method | Handler | Rate Limited | Output |
|---|---|---|---|---|
| /ota/github | POST | handleOtaGithub | Yes (OTA) | 302 redirect to /ota |
| /ota/url | POST | handleOtaUrl | Yes (OTA) | 302 redirect to /ota |
| /ota/upload | POST | otaUploadChunk | No (chunked) | Streaming chunked upload |
| /ota/status | GET | handleOtaStatusJson | No | otaProgress, status string |

### 2.7 Setup / Network Provisioning

| Route | Method | Handler | Output |
|---|---|---|---|
| /setup/scan | GET | handleSetupScan | WiFi scan results (SSID list) |
| /setup | GET | handleSetupGet | Setup portal HTML (SSID/PSK form) |
| /setup | POST | handleSetupPost | Writes WiFi credentials, reboots |
| /labels | GET | handleLabelsGet | Channel labels JSON (currently stub: {}) |
| /labels | POST | handleLabelsBody | Label persistence (currently stub: ok) |
| /autoupdate | POST | handleAutoUpdatePost | Toggles autoUpdate config flag |

### 2.8 Reset / Reboot Routes

| Route | Method | Handler | Output |
|---|---|---|---|
| /reset | GET | handleResetGet | Reset confirmation HTML |
| /reset | POST | handleResetPost | Clears NVS, reboots (requires confirm=1) |
| /reboot | POST | handleRebootPost | Soft reboot |

### 2.9 LED Route

| Route | Method | Handler | Output |
|---|---|---|---|
| /led/bright | GET | handleLedBright | Sets LED panel brightness (0-100) |

### 2.10 Diagnostics Route

| Route | Method | Handler | Output |
|---|---|---|---|
| /diag/soak-stats | GET | inline lambda | Soak-monitor heap/diagnostics JSON (only when LUXDMX_SOAK_TEST is defined) |

## 3. State Machine

The module maintains no persistent internal state machine. All handlers are stateless with respect to prior request history — each invocation reads the current configuration, stats, or RDM state and produces a response.

Two runtime flags are consumed (not owned) by specific handlers:
- The RDM poll-dirty flag (set by discovery and address-change operations; cleared by the main loop on the next iteration).
- The background-queue-policy dirty flag (set by /rdm/bqp; cleared by the main loop when persisted to NVS).

## 4. Data Flow

### 4.1 Config Save Flow (/config POST)

1. The handler checks for the "import" parameter; if present, it delegates to cfgcore::importJson on the body, then calls saveConfig and returns a reboot indicator.
2. Otherwise, it iterates the CONFIG_FIELDS table; for each field present in the request, it calls cfgcore::setValue with the field key and decoded value. Fields flagged CFG_REBOOT set the needsReboot flag.
3. It then iterates the OUTPUT_FIELDS table for each configured output; output keys carry a letter prefix (a_, b_, c_, d_) identifying the output index. Fields flagged CFG_REBOOT or CFG_LIVE are tracked accordingly.
4. If any field changed, saveConfig() is called. If any live output field changed, updateOutputRuntime is invoked for all outputs.
5. The response JSON reports the reboot requirement and the set of changed fields.

### 4.2 RDM Trigger Flow (/rdm/discover, /rdm/setaddr, etc.)

1. The handler extracts the "action" query parameter; if absent, HTTP 400 is returned.
2. For actions requiring a UID (setaddr, identify, setpers, setlabel), the handler parses the 12-character hex UID from the "uid" query parameter, splitting it into a 2-byte manufacturer ID and 4-byte device ID. If the UID is missing or malformed, HTTP 400 is returned.
3. The handler selects the target RDM output line via rdmOutSelect and invokes the corresponding RDM engine operation:
   - discover: sets the RDM poll-dirty flag.
   - setaddr: calls the address-change operation; sets the poll-dirty flag.
   - identify: toggles identify mode on the target for a bounded duration.
   - setpers: sets the active personality index.
   - setlabel: writes the device label string.
4. The response is a minimal acknowledgment.

### 4.3 OTA Initiation Flow (/ota/github, /ota/url)

1. The handler extracts the version or URL parameter; if absent, HTTP 400 is returned.
2. For /ota/github, the full firmware download URL is constructed from the version parameter.
3. The download target is stored in a shared OTA-target variable.
4. A FreeRTOS task is created on core 0 with an 8 KB stack to perform the download and flash in the background.
5. The handler issues an HTTP 302 redirect to /ota (the OTA progress page).

### 4.4 Health Snapshot Flow (/health)

1. The handler reads per-output signal status (fps, source, rx_frames, rx_loss) from the stats module.
2. It reads network link metrics (interface name, IP, link speed or RSSI) from the network state module.
3. It reads uptime and free heap.
4. The response JSON aggregates all fields with status "ok" when the device is operational.

## 5. Configuration Integration

| Config Field | Apply Semantics | Usage in Web Routes |
|---|---|---|
| artnetRdm | Live | Gate for RDM action routes — when disabled, RDM triggers are rejected |
| outputs[].enabled | Reboot | Gate for viewout selection and per-output RDM line selection |
| outputs[].universe | Live | Exposed in /info.json and /rdm.json per-output arrays |
| outputs[].mergeMode | Live | Set/returned via /rdm/merge; returned in /rdm.json |
| autoUpdate | Live | Toggled via /autoupdate POST |
| bqPolicy | Live | Set via /rdm/bqp; persisted by main loop |
| WiFi credentials (ssid/psk) | Reboot | Written via /setup POST |
| boardSel, mcu, etc. | — | Exposed read-only in /info.json device metadata |

The handler for /config iterates the schema field tables (CONFIG_FIELDS and OUTPUT_FIELDS) directly, so adding a field to the schema table automatically surfaces it in the POST /config save flow without additional handler code.

## 6. Lifecycle

1. **Init (setup phase 8):** All routes are registered during webRegisterRoutes(), which binds each HTTP path + method to its handler function on the global AsyncWebServer instance.
2. **Dispatch:** The AsyncWebServer event loop on core 0 invokes handlers on demand as HTTP requests arrive. No polling or periodic tick is required.
3. **No deinit.** Handlers are stateless; no cleanup is needed.

Rate-limited handlers are wrapped at registration time: the route table binds the wrapper, which checks the token bucket before delegating to the real handler.

## 7. Error Handling

| Condition | Handler | Response |
|---|---|---|
| Config import fails | handleConfigImport | HTTP 400 + error message |
| Missing config field key | handleConfigPost | Silently skipped (no field change) |
| Setup missing ssid/psk | handleSetupPost | HTTP 400 |
| Reset without confirm=1 | handleResetPost | HTTP 400 |
| RDM missing action parameter | handleRdmTrigger | HTTP 400 |
| RDM missing uid parameter | handleRdmTrigger | HTTP 400 |
| UID hex parse failure | handleRdmTrigger | HTTP 400 |
| OTA missing version/url | handleOtaGithub / handleOtaUrl | HTTP 400 |
| BQP value out of range (0-4) | handleRdmBqp | HTTP 400 |
| Merge mode out of range | handleRdmMerge | HTTP 400 |
| Merge output index out of range | handleRdmMerge | HTTP 400 |
| Rate limit exceeded | rateLimitHandler wrapper | HTTP 429 + Retry-After: 60 |
| Log JSON not implemented | handleLogJson | Returns [] (empty array) |
| Labels stub | handleLabelsGet / handleLabelsBody | Returns {} / "ok" |

## 8. Timing Constraints

- All handlers execute on core 0 in the AsyncWebServer/AsyncTCP event-task context at priority 5.
- OTA initiation handlers spawn a core-0 FreeRTOS task (8 KB stack) that runs the download independently of the HTTP response — the HTTP 302 redirect returns immediately.
- RDM trigger handlers execute synchronously on core 0 for the duration of the RDM transaction (after the core-1 DMX task has released the shared RMT channel for the current 1 ms tick). Discovery has an 8-second budget cap; setaddr and other operations are shorter.
- Config save writes to NVS (synchronous) within the config POST handler; any live config field that requires runtime update is applied before the response returns.
- The health and JSON snapshot handlers read volatile state via lock-free or seqlock primitives where cross-core data is involved (DMX buffer via seqlock; stats via atomic reads).

## 9. Memory and Allocation Model

- Handler-local JSON buffers are allocated on the stack or on the request's dynamic memory pool; no persistent heap allocation is owned by the module.
- The config save flow iterates field tables via index-based loops; no heap structures are allocated per field.
- OTA task stacks are allocated from the FreeRTOS heap (8 KB per task); these are freed when the OTA task completes or the system reboots.
- The UID parsing buffer is a fixed 33-byte character array (file-local static), not heap-allocated.
- Route registration strings (paths) are stored in read-only program memory via the AsyncWebServer registration API.

## 10. Safety Considerations

- **Core isolation:** All route handlers run on core 0, never preempting the core-1 DMX transmit task. RDM trigger handlers execute only after the core-1 RDM task has released the shared RMT peripheral for the current 1 ms frame tick, preventing contention with live DMX output.
- **Cross-core DMX buffer safety:** Handlers that write DMX channel data do so through the seqlock write protocol (write-begin, data, write-end), ensuring the core-1 transmit task never reads a torn frame.
- **Config NVS safety:** Configuration writes use the Config Engine's save path, which writes to NVS with clamping to field-defined min/max ranges. Live and reboot fields are distinguished so that hardware-bound settings are not applied until a restart.
- **Rate limiting:** Config-save and OTA-initiation endpoints are token-bucket limited (config: 30 req/min burst 60; OTA: 5 req/min burst 10), preventing abuse that could exhaust NVS write cycles or flash bandwidth.
- **Factory reset confirmation:** A factory reset requires an explicit confirm=1 parameter, preventing accidental mass erasure via a stray request.
- **UID validation:** RDM UID parameters are parsed and validated before any RDM operation is staged; malformed UIDs result in HTTP 400 with no side effects.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| net.web-server | registers handlers | Provides the AsyncWebServer instance and route-registration wrapper that binds paths to these handlers |
| net.rate-limiter | wraps handlers | Token-bucket rate limiting for /config POST and /ota/github, /ota/url |
| cfg.config-engine | reads / writes | Field tables (CONFIG_FIELDS, OUTPUT_FIELDS), setValue, exportJson, importJson, save |
| core.stats | reads | Health output (output status, sender info), RDM counters, TOD table |
| core.rdm-engine | invokes | RDM discovery, address change, identify, personality, label operations |
| core.output-init | invokes | rdmOutSelect for RDM action route line selection |
| core.rdm-task | deferred consumer | Processes RDM operations staged by trigger handlers |
| net.ota | invokes | otaFromGithub, otaFromUrl for background OTA tasks |
| sys.sys-platform | reads | otaTarget global, latestVersion, updateAvailable, otaProgPct |
| sys.firmware-version | reads | version, build, variant for /info and /version JSON |
| sys.soak-monitor | reads | soakStatsJson for /diag/soak-stats |
| net.net-state | reads | Network link metrics (interface name, IP, RSSI) for /health |
| core.dmx-buffer | reads via seqlock | DMX snapshot for /dmx.json (snapshotAndTransmit read path) |

## 12. Testing Verification

- No host-native unit tests cover the web routes handlers directly.
- The config save flow, RDM trigger actions, OTA initiation, and health snapshot are exercised end-to-end through browser-based E2E tests (Playwright) that issue HTTP requests against a live device and verify JSON response structure and field values.
- Rate-limit behavior (HTTP 429) is validated by the E2E suite through burst request testing.
- The schema-driven config form is verified by checking that every field in CONFIG_FIELDS appears in the /config GET response and round-trips through POST /config.
- The RDM TOD endpoint is validated by observing UID population in /rdm.json before and after discovery.
- The soak-stats route is conditionally tested only in soak-test builds.

**Untested paths:**
- Config import JSON parsing error messages.
- OTA chunked-upload stream integrity in isolation.
- Health endpoint network-link metric correctness across WiFi and wired interfaces.
- Label persistence (currently stubbed on the server side).

## 13. Open Questions

1. Whether the log JSON endpoint should be fully implemented to return change-log entries, or remain stubbed indefinitely.
2. Whether the label persistence endpoints (/labels GET/POST) should be promoted from stub to real NVS-backed storage, or kept browser-local in localStorage.
3. Whether the health endpoint should expose a deeper alert-level field (e.g., warning/error) beyond the binary status "ok".
4. Whether /version.json's latestVersion comparison should fetch the remote release manifest on-device, or rely on the browser-side fetch that currently occurs in the frontend JavaScript.
5. Whether the config import error path should return structured JSON rather than plain text.

## 14. History

No recorded changes.
