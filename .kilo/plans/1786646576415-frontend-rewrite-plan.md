# Frontend Rewrite Plan — LuxDMX-v2

## Problem Summary

The web frontend has two confirmed bugs:

1. **Missing configuration** — `handleInfoJson` in `src/net/web_routes.cpp:81-101` returns only ~12 fields
   (hostname, version, protocol, board, mcu, rssi...) but the config page needs ~40 fields
   (ledType, ledPin, dispType, outputs[], wifiSsid, staticIp, ethW5500, rmiiPhy...).
   The form renders placeholders and Save is disabled when `/info.json` fails to provide config.

2. **No control panel** — `_nav.html` defines a shared navbar (navigation tabs + real-time
   stats via `LuxNav`) but **never gets injected** into any page. `extra_scripts.py` only
   generates PROGMEM headers from raw HTML; the `<!--NAVBAR-->` markers are just comments.
   Every page is missing navigation, status bar, and the `LuxNav.stats()` API.

Secondary issues:
- `handleConfigPost` returns plain text, but config.html's submit handler expects JSON.
- `/rdm/bqp` and `/rdm/merge` routes don't exist but rdm.html calls them.
- `handleRdmJson` returns only 5 fields; rdm.html expects 20+.

## Design System (via UI-UX-Pro-Max skill)

Generated from `nextlevelbuilder/ui-ux-pro-max-skill` — Developer Tool / IDE category,
Dark Mode (OLED) + Data-Dense Dashboard / Real-Time Monitoring patterns:

| Decision | Value |
|---|---|
| **Style** | Dark Mode (OLED), Real-Time Monitoring |
| **Background** | `#0d1117` (deep black) — current CSS already matches |
| **Accent** | Cyan `#23e6f7` (live), Amber `#ffaa1c` (warnings), Magenta `#f33abc` (errors) |
| **Typography** | JetBrains Mono (headings) + IBM Plex Sans (body) |
| **Layout** | Card-based sections, collapsible/expandable, sticky navbar, responsive |
| **Real-time** | WebSocket binary frames (existing 2090-byte protocol) + text meta push |
| **Constraints** | keyboard-shortable, form labels required, inline validation, submit feedback |
| **Anti-patterns** | Avoid: light mode default, slow performance |

The existing CSS already uses the correct dark palette and brand colors — the main fixes
are structural (navbar injection, config data completeness).

## Approach: Component-based PROGMEM embedding

Replace the `extra_scripts.py` HTML→`.h` generation pipeline with **directly embedded
PROGMEM strings** in C++ files, organized as reusable components in a new `src/frontend/`
directory. This eliminates the build-time Python generation step and makes the frontend
code directly editable as C++ source.

### File structure (new `src/frontend/`)

```
src/frontend/
├── base/
│   ├── styles.h             # Shared CSS: theme vars, dark mode, Bootstrap overrides,
│                             # channel grid CSS, modal CSS, collapsible sections, etc.
│   ├── navbar.h             # Shared <nav> markup + LuxNav IIFE (from _nav.html)
│   ├── footer.h             # Shared footer markup
│   └── icons.h              # Inline SVG icons (caret, pin-pick, etc.)
├── components/
│   ├── channel_grid.h       # DMX 512-grid cell markup + tooltip
│   ├── channel_modal.h      # Single-channel edit modal
│   ├── senders_table.h      # Active senders table markup
│   ├── changelog.h          # Change log list markup
│   ├── update_banner.h      # Firmware update banner + confirm modal
│   ├── output_selector.h    # Output A/B/C/D selector buttons
│   ├── confirm_modal.h      # Generic confirm/info modal (app-modal)
│   ├── board_picker.h       # Pin-picker board descriptors (issue #12)
│   ├── ota_progress.h       # OTA progress spinner + bar
│   ├── rdm_fixture_row.h    # RDM fixture table row
│   ├── rdm_sensor_chart.h   # SVG sensor chart
│   ├── rdm_merge_ctl.h      # Per-output merge mode selector
│   ├── rdm_discovery.h      # Discovery confirmation modal
│   └── setup_stepper.h      # Setup portal step navigation
├── pages/
│   ├── index_page.h         # Status page body
│   ├── config_page.h        # Settings page body
│   ├── rdm_page.h           # RDM fixtures page body
│   ├── setup_page.h         # Setup portal page body
│   ├── reset_page.h         # Reset WiFi page body
│   ├── ota_pages.h          # OTA progress + done bodies
│   └── status_pages.h       # config_saved / setup_done / reset_done bodies
├── scripts/
│   ├── shared_js.h          # JS utilities (showModal, esc, vNum, fetchJson)
│   ├── index_js.h           # JS: websocket, DMX grid, senders, changelog, update check
│   ├── config_js.h          # JS: form population, validation, save/submit
│   ├── board_picker_js.h    # JS: pin picker board descriptors + diagram logic
│   └── rdm_js.h             # JS: fixture table, sensor charts, discovery, RDM ops
├── web_frontend.h           # Public API: page serving functions
└── web_frontend.cpp         # Handler implementations (replaces web_pages.cpp)
```

### Embedding strategy

Each `.h` file uses `R"=====( ... )====="` raw string literals with `PROGMEM`:

```cpp
// frontend/base/navbar.h
#pragma once
#include <Arduino.h>

static const char NAV_HTML[] PROGMEM = R"=====(
<nav class="site-nav">...</nav>
)=====";

static const char NAV_JS[] PROGMEM = R"=====(
<script>
window.LuxNav = (function(){...})();
</script>
)=====";
```

**Serving approach**: Since raw string literals can't concatenate across includes, the
`web_frontend.cpp` handlers stream the page parts through
`AsyncWebServerResponse::beginChunkedResponse()` / `beginResponse_P()`. For single-string
pages (setup, reset, OTA done), use `beginResponse_P()` directly.

For navbar injection (all pages), the handler streams:
1. `<!DOCTYPE html>...<head>...<style>` + base styles from `styles.h`
2. `</style></head><body>`
3. Navbar HTML from `navbar.h`
4. Page body markup from `pages/*.h`
5. Shared JS from `scripts/shared_js.h` + page-specific JS from `scripts/*.h`
6. Footer from `footer.h`
7. `</body></html>`

**Flash cost**: Removing gzip saves ~60% flash but adds ~60 KB uncompressed. ESP32-S3
targets have 8-16 MB flash — acceptable trade-off for a simpler build.

### Bug fixes required

1. **Fix `handleInfoJson`** (`web_routes.cpp:81-101`) — return full config:
   - `outputs` array: enabled, universe, merge, txRate, txStyle, mode
   - All LED fields: ledType, ledPin, ledR/G/Y/B/W, ledBrR/G/Y/B/W
   - All display fields: dispType, dispSda/Scl/Rot, dispCs/Dc/Rst/Sck/Mosi
   - All control fields: encA/B/Sw, encSteps, encReverse, btn1-4Pin, btn1-4Act, btnActiveHigh, ctlUniMax
   - Network fields: wifiMode, wifiSsid, apPassword, linkLossMode, staticIp, ip, gateway, subnet, dns, ipProg, autoIpFallback, dscpEnabled, dscpDmx, vlanEnabled, vlanId
   - Ethernet fields: useEthernet, ethW5500, ethSpiPhy, wiredPhy, rmiiPhy, rmiiAddr, rmiiMdc, rmiiMdio, rmiiPwr, rmiiClk, ethSpi, ethRmii, hasEth, eth_ip, eth_speed
   - Device fields: hostname, otapw, boardSel, autoUpdate, artnetRdm, rdmMaxDev

2. **Fix `handleConfigPost`** (`web_routes.cpp:181-237`) — return JSON:
   ```json
   {"reboot": true, "fields": "field1, field2..."}
   ```

3. **Add missing RDM routes** in `web_server.cpp`:
   - `/rdm/bqp` (GET) → set Art-Net BackgroundQueue policy
   - `/rdm/merge` (GET) → set output merge mode

4. **Fix `handleRdmJson`** (`web_routes.cpp:115-123`) — return full RDM state:
   - `devices` array with uid, label, model, mfg, pers, persCount, addr, footprint, cat, identify, subs, sensors, stCount, stType, stId, sw
   - `outputs` array (i, uni, merge)
   - `rdmLines` array (line, uni)
   - `discStage`, `discFound`, `discCur`, `discSub`, `discovering`, `busy`
   - `available`, `scanned`, `sensorPoll`, `bqPolicy`
   - `artPort`, `artPolls`, `artTodReqs`, `artRdmReqs`, `artFlushes`

### Implementation phases

**Phase 1 — Fix backend data endpoints (blocking bugs)**
1. Update `handleInfoJson` in `web_routes.cpp` to return full config
2. Update `handleConfigPost` to return JSON `{reboot, fields}`
3. Update `handleRdmJson` to return full RDM state
4. Add `/rdm/bqp` and `/rdm/merge` route handlers in `web_routes.cpp`
5. Register new routes in `web_server.cpp:14-64`

**Phase 2 — Create frontend base layer**
1. Create `src/frontend/base/styles.h` (consolidate CSS from all HTML files)
2. Create `src/frontend/base/navbar.h` (from `_nav.html`)
3. Create `src/frontend/base/footer.h` (from existing footers)
4. Create `src/frontend/base/icons.h` (SVG icons from HTML)
5. Create `src/frontend/scripts/shared_js.h` (utilities used by all pages)

**Phase 3 — Create frontend pages and components**
1. Status page: `pages/index_page.h` + `components/channel_grid.h` + `components/channel_modal.h`
   + `components/senders_table.h` + `components/changelog.h` + `components/update_banner.h`
   + `components/output_selector.h` + `components/confirm_modal.h` + `scripts/index_js.h`
2. Config page: `pages/config_page.h` + `components/board_picker.h` + `scripts/config_js.h`
   + `scripts/board_picker_js.h` + `components/confirm_modal.h`
3. RDM page: `pages/rdm_page.h` + `components/rdm_fixture_row.h` + `components/rdm_sensor_chart.h`
   + `components/rdm_merge_ctl.h` + `components/rdm_discovery.h` + `scripts/rdm_js.h`
4. Utility pages: `pages/setup_page.h` + `pages/reset_page.h` + `pages/ota_pages.h`
   + `pages/status_pages.h` + `components/setup_stepper.h` + `components/ota_progress.h`

**Phase 4 — Wire up serving**
1. Create `src/frontend/web_frontend.h` with serve function declarations
2. Create `src/frontend/web_frontend.cpp` with streaming handlers
3. Update `src/net/web_server.cpp` to use new handlers
4. Update `src/main.cpp` includes

**Phase 5 — Clean up**
1. Remove old `src/pages/*.html` files
2. Remove HTML-to-.h generation from `extra_scripts.py` (keep binary asset generation)
3. Delete old `src/net/web_pages.h` / `web_pages.cpp`
4. Delete `src/generated/` directory

### Validation

- Build compiles with `pio run -e esp32s3dev`
- Config page shows current settings (not placeholders) when `/info.json` is reachable
- Navbar appears on all pages with working Status/RDM/Settings tab navigation
- Navbar stats update via `LuxNav.stats()` on every WebSocket binary frame
- RDM page shows fixture list, sensor charts, discovery button
- Config save returns JSON, triggers reboot modal when needed
- OTA update flow works end-to-end
- WebSocket real-time DMX grid shows live channel values
- No heap fragmentation from PROGMEM streaming

## Risk: RDM device data source

The `handleRdmJson` handler needs device data (fixtures, sensors) that currently only
exists in the RDM discovery module. If `rdm_disc.h` / `rdm_engine.h` don't expose a
function to serialize device state as JSON, adding one is a prerequisite. The plan assumes
this function can be added — if the RDM state structures are not externally accessible,
that is a follow-up task outside this frontend rewrite.
