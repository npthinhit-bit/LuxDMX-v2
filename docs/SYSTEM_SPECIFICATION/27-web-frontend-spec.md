# Web Frontend — System Specification

Domain: net.web-frontend

## 1. Module Overview

The Web Frontend subsystem delivers the browser-side user interface for the LuxDMX gateway. It assembles complete HTML documents from a set of PROGMEM-stored fragments (CSS, HTML body, JavaScript), serves them as cached or non-cached HTTP responses, and injects the firmware version token into every served page. The frontend is the user's window into all device functionality: live DMX channel monitoring and control, sender/source tracking, RDM device management, configuration editing, OTA updates, and network provisioning (setup portal, factory reset).

The frontend is composed of two complementary halves:
- **Server-side page assembly:** fragments stored as PROGMEM string constants, concatenated with a shared layout (navbar, footer, modal, shared styles and scripts), with {{PLACEHOLDER}} token substitution for the firmware version.
- **Browser-side JavaScript:** a WebSocket client that receives binary status frames and text meta pushes, a set of page-specific scripts for DMX control, scene management, RDM operations, and config form generation, and browser-side state management persisted in localStorage.

The frontend runs as part of the core-0 HTTP server task; the browser-side JavaScript runs entirely in the client browser on the user's machine.

**Owns:** HTML page assembly (sendAppPage, sendRawPage), PROGMEM fragment set, firmware-version token injection.
**Produces:** HTTP HTML responses (index, config, RDM, setup, reset, OTA, done/saved pages).
**Consumes:** WebSocket binary frames (live DMX/stats), /info.json and /rdm.json (config and fixture population), WebSocket text commands (channel set, blackout, identify, scene, RDM actions), /ota/github POST (OTA initiation), /labels.json + POST /labels (client-side label persistence), /autoupdate POST (auto-update toggle).

## 2. External Interfaces

### 2.1 Served HTML Routes

| Route | Method | Page Type | Fragments Used |
|---|---|---|---|
| / | GET | App page | INDEX_PAGE_BODY, INDEX_PAGE_CSS, INDEX_PAGE_JS |
| /config | GET | App page | CONFIG_PAGE_BODY, CONFIG_PAGE_CSS, CONFIG_PAGE_JS |
| /rdm | GET | App page | RDM_PAGE_BODY, RDM_PAGE_CSS, RDM_PAGE_JS |
| /setup | GET | Raw page | SETUP_PAGE |
| /reset | GET | Raw page | RESET_PAGE |
| /ota | GET | Raw page | OTA_PROGRESS_PAGE |
| /config/saved | GET | Raw page | CONFIG_SAVED_PAGE |
| /setup/done | GET | Raw page | SETUP_DONE_PAGE |
| /reset/done | GET | Raw page | RESET_DONE_PAGE |

### 2.2 Fragment Inventory

**Base fragments (shared layout):**
- FRONTEND_STYLES — global CSS (variables, grid, cards, modals).
- NAVBAR_CSS, NAVBAR_HTML, NAVBAR_JS — status bar, navigation links, binary-frame stats decoder.
- FOOTER_HTML — copyright footer.
- APP_MODAL_HTML, SHARED_JS — shared modal dialog and utility functions (esc, vNum, extractChanges, showModal).
- ICON_CARET, ICON_PICK — SVG icon fragments.

**Page-specific fragments:** INDEX, CONFIG, RDM bodies/CSS/JS; SETUP, RESET, OTA_PROGRESS, OTA_DONE, CONFIG_SAVED, SETUP_DONE, RESET_DONE raw pages.

### 2.3 WebSocket Interface

| Direction | Frame Type | Consumer | Producer |
|---|---|---|---|
| Device → Browser | Binary (2095 bytes) | Index page JS, RDM page JS | WebSocket protocol push (~10 Hz) |
| Device → Browser | Text (JSON meta) | Index page JS | WebSocket meta push (2 Hz) |
| Browser → Device | Text (JSON command) | WebSocket handler | Index page JS, RDM page JS |

### 2.4 HTTP API Consumed by Frontend

| Endpoint | Consumed By | Purpose |
|---|---|---|
| /info.json | Config JS, Index JS | Device metadata, config field population, board selection |
| /rdm.json | RDM JS | RDM status, fixture list, line mapping |
| /version.json | Index JS | Firmware version, update availability, OTA progress |
| /labels.json + POST /labels | Index JS | Channel label persistence (browser-local via localStorage) |
| /ota/github (POST) | Index JS | OTA initiation from GitHub release |
| /autoupdate (POST) | Config JS | Auto-update toggle |
| /ota/url (POST) | Config JS | Manual URL-based OTA initiation |

## 3. State Machine

The frontend has no server-side state machine — it is stateless template assembly. The browser-side JavaScript maintains client state:

- **Output selection:** the active monitored output index, persisted across navigation via the viewout WebSocket command.
- **Section collapse state:** per-config-card open/closed state, persisted in localStorage under the key `lux_cfg_sections`.
- **Bulk expand/collapse:** a summary state for "expand all" / "collapse all" in the config page.
- **RDM sensor history:** rolling 60-sample window per sensor, persisted in localStorage under `rdmhist`.
- **WebSocket connection state:** connected (Live), disconnected (Offline), with auto-reconnect after 2 seconds.

The firmware-version token (`__FWVER__`) is injected at serve time; the browser caches HTML with `Cache-Control: no-cache` but cache-busts external assets (favicon, Bootstrap CSS, logo) via the `?v=` query parameter.

## 4. Data Flow

### 4.1 HTML Page Assembly (sendAppPage)

1. A reserved String buffer (20 KB preallocated) is constructed with the page prologue (DOCTYPE, html, head with meta tags, title, favicon link, Bootstrap CSS link, inline style block).
2. Shared fragments are appended: FRONTEND_STYLES, NAVBAR_CSS, page-specific CSS.
3. The body opens: NAVBAR_HTML, then the inline NAVBAR_JS script, then the page-specific body content.
4. The page closes with FOOTER_HTML, APP_MODAL_HTML, SHARED_JS script, and the page-specific JS script.
5. The firmware-version token `__FWVER__` is replaced with the actual FIRMWARE_VERSION string via in-place String replacement.
6. The assembled HTML is returned as an HTTP 200 response with `text/html` content type and `Cache-Control: no-cache`.

### 4.2 Static Asset Caching

External assets (favicon, Bootstrap CSS, logo) are served with `Cache-Control: max-age=604800` (one week) and the `?v=__FWVER__` query parameter busts the cache on firmware version change. Bootstrap CSS is additionally delivered with `Content-Encoding: gzip`. HTML pages are served with `no-cache` to ensure the browser always fetches the latest firmware version.

### 4.3 WebSocket Live Data Flow

1. The browser opens a WebSocket to `/ws` on the same host.
2. The device pushes a 2095-byte binary frame at ~10 Hz, containing: header stats (fps, RSSI, heap, uptime, sender count, source status, jitter), per-output DMX data (512 channels × 4 outputs), per-output TX/RX fps, TX style, and a navigation tail (fixture count, RDM TX/RX counts).
3. The navbar JS (shared across all pages) decodes the binary frame via a DataView, updating the status bar with link status, signal strength, and output indicators.
4. The index page JS extracts the monitored output's 512 DMX channels from the binary frame and renders them in a 512-cell grid, with live gauge bars.
5. The browser opens a separate WebSocket connection on the RDM page for the same `/ws` endpoint.

### 4.4 Meta Push (Text Frames)

1. The device pushes a text-frame JSON payload at 2 Hz containing sender list and change-log entries.
2. The index page JS parses the meta push and updates the sender table and change-log table without polling.
3. A `canSend()` guard prevents binary pushes when no clients are connected.

### 4.5 Config Form Generation

1. On page load, the config JS fetches /info.json and populates all form fields from the exported config.
2. The output configuration section is generated from a client-side template (`<template id="out-tpl">`), cloning one card per configured output with letter-prefixed fields (a_, b_, c_, d_).
3. Wired-Ethernet selection uses a single dropdown that maps to hidden platform-specific firmware fields.
4. Pin pickers display a board-diagram modal with hardware-specific pin constraints (flash, serial, strapping, USB-JTAG, reserved).
5. On submit, the form is serialized to FormData and POSTed to /config; the response indicates whether a reboot is required and which fields need it.

### 4.6 RDM Fixture Management

1. On page load, the RDM JS fetches /rdm.json and /rdm/tod to populate the fixtures table.
2. The fixtures table supports column sorting, row expansion for per-fixture editing, and inline sensor charts (SVG sparklines with 60-sample rolling history).
3. RDM commands (discover, setaddr, setlabel, setpers, identify, sensor toggle) are sent as WebSocket text frames and staged for deferred execution on core 0.
4. HTTP fallbacks exist for merge mode and BQP: /rdm/merge and /rdm/bqp are called via fetch.
5. Polling frequency adapts to activity: 600 ms during discovery, 1 s with live sensors, 3 s otherwise.

### 4.7 OTA Update Flow

1. The index page JS fetches /version.json and then the remote release manifest from the LuxDMX firmware releases endpoint.
2. The current version is compared to the latest using the vNum() numeric encoding.
3. Newer release changelog entries are aggregated and displayed.
4. The user initiates installation by submitting POST /ota/github with the selected version.
5. The device redirects (302) to /ota, which serves the OTA progress page; the browser polls /ota/status for progress.

## 5. Configuration Integration

The frontend has no server-side configuration consumption. All config field population is driven by the browser-side JavaScript consuming the /info.json endpoint, which merges the Config Engine's JSON export with firmware metadata. The config page renders form fields dynamically from this JSON — no field is hardcoded in the frontend beyond the output template structure and the hardware-specific board descriptor tables embedded in config JS.

Board-specific pin constraints, display types, LED types, and Ethernet PHY options are encoded as client-side descriptor tables within the config JS fragment; these are compile-time constants in the PROGMEM fragment, not runtime configuration.

## 6. Lifecycle

1. **Page serve:** HTML is assembled and served on-demand from PROGMEM fragments when the browser requests a route. No pre-rendering or caching beyond the browser's HTTP cache.
2. **Browser load:** Page-specific JS initializes on DOMContentLoaded, fetches required JSON endpoints, opens WebSocket(s), and registers event listeners.
3. **Live session:** The WebSocket connection drives all live updates; the browser maintains state in memory and localStorage.
4. **Session end:** WebSocket disconnects auto-reconnect after 2 seconds; no explicit cleanup.

The server-side assembly fragments are static PROGMEM constants — they are never modified after compilation. The only runtime mutation is the firmware-version token substitution per request.

## 7. Error Handling

| Condition | Location | Behavior |
|---|---|---|
| WebSocket disconnect | Index JS, RDM JS | Badge turns "Offline" (magenta); auto-reconnect after 2 s |
| /info.json fetch failure | Config JS | Renders skeleton form with disabled save button; shows error modal |
| /rdm.json fetch failure | RDM JS | Silently caught; polling continues |
| /version.json fetch failure | Index JS | Update banner shows "Could not reach LuxDMX.org" |
| Form submission failure | Config JS | Restores save button state; shows "device did not report back" modal |
| Binary frame parse error | Navbar JS | Stats display stops updating; reconnects on next WebSocket cycle |
| Meta push parse error | Index JS | Sender/log tables stop updating; no crash |
| OTA release fetch failure | Index JS | Shows "Could not reach LuxDMX.org" message |
| Pin conflict | Config JS | Warning shown on the conflicting pin selectors; save is not blocked |
| Missing output TX pin on save | Config JS | Client-side validation prevents save; error message shown |

## 8. Timing Constraints

- WebSocket binary push occurs at ~10 Hz (100 ms period); the navbar and channel grid update on each frame.
- WebSocket meta push occurs at 2 Hz (500 ms period); sender and log tables update on each push.
- RDM page polling frequency adapts: 600 ms during discovery, 1 s with live sensors, 3 s otherwise.
- OTA progress is polled via /ota/status; the device updates otaProgPct in the global platform state, read by the status endpoint.
- The firmware-version token substitution (String::replace) occurs per HTML request; the 20 KB reservation avoids heap fragmentation on repeated serves.
- A heap guard of 40 KB is enforced before the meta push to prevent out-of-memory conditions on the device.

## 9. Memory and Allocation Model

- All HTML/CSS/JS fragments are stored as PROGMEM string constants (FPSTR macros), consuming program memory rather than DRAM.
- The server-side assembly String is preallocated to 20 KB (html.reserve(20000)) to avoid heap fragmentation during fragment concatenation.
- The binary WebSocket frame is 2095 bytes (fixed-size static buffer on the device; the browser receives it as an ArrayBuffer and decodes via DataView without copying).
- Browser-side: the 512-cell channel grid is built once at page load; per-cell updates are done by rewriting textContent and style properties in place (no DOM re-creation).
- Browser-side: RDM sensor history is capped at 60 samples × 320 px × number of sensors per localStorage entry under `rdmhist`; total browser localStorage usage is bounded to a few hundred kilobytes.
- Browser-side: config section collapse state is a compact bitmask stored under `lux_cfg_sections`.

## 10. Safety Considerations

- **No destructive action without confirmation:** Factory reset requires an explicit `confirm=1` parameter on the POST /reset endpoint; the frontend enforces a confirmation checkbox before submission.
- **OTA rate limiting:** GitHub and URL OTA initiation endpoints are rate-limited (5 req/min, burst 10) at the server layer, preventing runaway flash erase/write cycles.
- **Config live vs. reboot semantics:** The frontend respects the CFG_LIVE / CFG_REBOOT flags from the schema; reboot-requiring changes are flagged in the response and the user is notified via a modal listing the affected fields. The device is not restarted until the user acknowledges.
- **RDM execution isolation:** RDM commands staged via WebSocket are executed synchronously on core 0 only after the core-1 DMX task has released the RMT peripheral for the current 1 ms tick, preventing any interruption of the live DMX stream.
- **Pin validation:** The pin picker enforces board-specific constraints (strapping pins, flash-connected pins, USB-JTAG pins, input-only pins) and warns on conflicts before allowing a save.
- **No credentials in HTML:** Configuration secrets are never embedded in PROGMEM fragments; they are served only through the /config/export endpoint with optional redaction.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| net.web-server | served by | Provides the AsyncWebServer instance and route-registration binding for all HTML routes |
| net.websocket | consumes | Binary status frames (~10 Hz) and text meta pushes (2 Hz) for live updates |
| net.websocket-handler | invoked by | Receives browser-originated WebSocket text commands (channel set, blackout, identify, scene, RDM actions) |
| cfg.config-engine | consumed via /info.json | Exports config fields for form population; fields auto-surface in the config form |
| core.rdm-engine | consumed via /rdm.json and /rdm/tod | Provides RDM device count, TOD table, sent/recv counters, line-to-output mapping |
| core.stats | consumed via binary frame | Provides per-output fps, sender count, rx_loss, jitter for the navbar |
| core.scene-engine | invoked via WebSocket | Scene play, save, and clear commands |
| core.dmx-buffer | consumed via binary frame | DMX channel data for the monitored output, snapshot via seqlock |
| net.web-routes | consumed | /info.json, /rdm.json, /version.json, /ota/github, /labels, /autoupdate endpoints |
| sys.firmware-version | consumed | __FWVER__ token substitution, /version.json metadata |
| sys.soak-monitor | consumed via /diag/soak-stats (conditional) | Soak-test diagnostics |
| net.rate-limiter | upstream | Rate-limits POST /config and POST /ota endpoints before reaching the frontend's HTTP consumers |

## 12. Testing Verification

- No host-native unit tests cover the PROGMEM fragment assembly or the JavaScript logic.
- Browser-based E2E tests (Playwright) exercise the full frontend lifecycle: page load, JSON endpoint consumption, WebSocket live-frame decoding, channel grid rendering, config form population and save, RDM fixture table population and command dispatch, and OTA initiation.
- The navbar binary-frame decoder (`stats(buf)` function) is a runtime contract validated by E2E tests that compare decoded navbar values against the device's reported state.
- The WebSocket command contract (channel set, blackout, identify, scene, RDM actions) is verified end-to-end by driving commands from the browser and observing device-side state changes through the next binary frame.

**Untested paths:**
- Edge cases in the 20 KB String reservation under memory pressure on the device.
- localStorage quota exhaustion for RDM sensor history and config section state.
- Pin-conflict warning accuracy across all board descriptors.
- The full config form round-trip for every OUTPUT_FIELDS entry under all board templates.
- RDM page separate-WebSocket connection lifecycle (both browser sockets share the same /ws endpoint).

## 13. Open Questions

1. Whether the RDM page should share a single WebSocket connection with the index page, or whether the separate connection provides a measurable benefit.
2. Whether the 20 KB String reservation for HTML assembly is sufficient for all page variants under tight heap conditions, or whether a streaming response API would be more robust.
3. Whether the client-side pin-conflict detection should be promoted to a server-side validation in the config POST handler as a defense-in-depth measure.
4. Whether the log JSON endpoint (currently returning []) should be fully implemented to drive the change-log table, or whether the meta-push text frame is the intended sole source.
5. Whether the RDM sensor-chart history persisted in localStorage should have a TTL-based eviction policy to bound storage growth over long sessions.

## 14. History

No recorded changes.
