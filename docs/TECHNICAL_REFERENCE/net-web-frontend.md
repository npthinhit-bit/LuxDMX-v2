# Web Frontend — Technical Reference

| Attribute | Value |
|---|---|
| **Layer** | `net` (frontend serving) |
| **Module** | `web_frontend` |
| **Source Files** | `src/frontend/web_frontend.h`, `src/frontend/web_frontend.cpp` |
| **Fragment Sources** | `src/frontend/base/*.h`, `src/frontend/pages/*.h`, `src/frontend/scripts/*.h` |
| **Related** | [Web Server](net-web-server.md), [Web Routes](net-web-routes.md), [WebSocket Protocol](net-websocket-protocol.md) |

## 1. File Inventory

### 1.1 Core Module
| Source File | Purpose |
|---|---|
| `src/frontend/web_frontend.h` | Page handler declarations (root, config, rdm, setup, reset, OTA, config-saved, setup-done, reset-done) |
| `src/frontend/web_frontend.cpp` | HTML assembly and serving via `sendAppPage` and `sendRawPage` |

### 1.2 Base Fragments (`src/frontend/base/`)
| Fragment | Symbol | Source |
|---|---|---|
| `src/frontend/base/styles.h` | `FRONTEND_STYLES` — global CSS (CSS variables, grid, cards, modals) | `src/frontend/base/styles.h:4` |
| `src/frontend/base/navbar.h` | `NAVBAR_CSS`, `NAVBAR_HTML`, `NAVBAR_JS` — status bar + nav + stats decoder | `src/frontend/base/navbar.h:4,18,43` |
| `src/frontend/base/footer.h` | `FOOTER_HTML` — copyright footer | `src/frontend/base/footer.h:4` |
| `src/frontend/base/icons.h` | `ICON_CARET`, `ICON_PICK` — SVG icon fragments | `src/frontend/base/icons.h:4,6` |

### 1.3 Page Fragments (`src/frontend/pages/`)
| Fragment | Symbols | Source |
|---|---|---|
| `src/frontend/pages/index_page.h` | `INDEX_PAGE_BODY` — status page HTML | `src/frontend/pages/index_page.h:4` |
| `src/frontend/pages/index_css.h` | `INDEX_PAGE_CSS` — status page CSS | `src/frontend/pages/index_css.h:4` |
| `src/frontend/scripts/index_js.h` | `INDEX_PAGE_JS` — status page JavaScript (grid, channels, WebSocket, senders) | `src/frontend/scripts/index_js.h:4` |
| `src/frontend/pages/config_page.h` | `CONFIG_PAGE_BODY` — config form HTML | `src/frontend/pages/config_page.h:4` |
| `src/frontend/pages/config_css.h` | `CONFIG_PAGE_CSS` — config form CSS | `src/frontend/pages/config_css.h:4` |
| `src/frontend/scripts/config_js.h` | `CONFIG_PAGE_JS` — config form JS (board picker, pin validation, save) | `src/frontend/scripts/config_js.h:4` |
| `src/frontend/pages/rdm_page.h` | `RDM_PAGE_BODY` — RDM fixtures table HTML | `src/frontend/pages/rdm_page.h:4` |
| `src/frontend/pages/rdm_css.h` | `RDM_PAGE_CSS` — RDM page CSS | `src/frontend/pages/rdm_css.h:4` |
| `src/frontend/scripts/rdm_js.h` | `RDM_PAGE_JS` — RDM fixtures table JS (discovery, sensors, sorting) | `src/frontend/scripts/rdm_js.h:4` |
| `src/frontend/pages/setup_page.h` | `SETUP_PAGE` — WiFi setup portal HTML | `src/frontend/pages/setup_page.h:4` |
| `src/frontend/pages/setup_done_page.h` | `SETUP_DONE_PAGE` | `src/frontend/pages/setup_done_page.h:4` |
| `src/frontend/pages/reset_page.h` | `RESET_PAGE` | `src/frontend/pages/reset_page.h:4` |
| `src/frontend/pages/reset_done_page.h` | `RESET_DONE_PAGE` | `src/frontend/pages/reset_done_page.h:4` |
| `src/frontend/pages/ota_progress_page.h` | `OTA_PROGRESS_PAGE` | `src/frontend/pages/ota_progress_page.h:4` |
| `src/frontend/pages/ota_done_page.h` | `OTA_DONE_PAGE` | `src/frontend/pages/ota_done_page.h:4` |
| `src/frontend/pages/config_saved_page.h` | `CONFIG_SAVED_PAGE` | `src/frontend/pages/config_saved_page.h:4` |

### 1.4 Shared Scripts
| Fragment | Symbol | Source |
|---|---|---|
| `src/frontend/scripts/shared_js.h` | `SHARED_JS` — `esc()`, `vNum()`, `extractChanges()`, `showModal()` + `APP_MODAL_HTML` | `src/frontend/scripts/shared_js.h:4,42` |

## 2. HTML Assembly Pipeline

### 2.1 `sendAppPage` — App Pages With Shared Layout

Source: `src/frontend/web_frontend.cpp:26`

```
sendAppPage(req, pageBody, pageCss, pageJs):
    html.reserve(20000)                          // src/frontend/web_frontend.cpp:28
    html += <!DOCTYPE html><html data-bs-theme="dark"><head>   // src/frontend/web_frontend.cpp:29
    html += <meta charset>, <meta viewport>
    html += <title>LuxDMX</title>
    html += <link rel="icon" href="/favicon.png?v=__FWVER__">
    html += <link rel="stylesheet" href="/bootstrap.min.css?v=__FWVER__">
    html += <style>
    html += FRONTEND_STYLES                       // src/frontend/web_frontend.cpp:35
    html += NAVBAR_CSS                            // src/frontend/web_frontend.cpp:36
    html += pageCss (optional)                     // src/frontend/web_frontend.cpp:37
    html += </style></head><body>
    html += NAVBAR_HTML                           // src/frontend/web_frontend.cpp:39
    html += <script>NAVBAR_JS</script>             // src/frontend/web_frontend.cpp:41-42
    html += pageBody                              // src/frontend/web_frontend.cpp:43
    html += FOOTER_HTML                           // src/frontend/web_frontend.cpp:44
    html += APP_MODAL_HTML                        // src/frontend/web_frontend.cpp:45
    html += <script>SHARED_JS</script>             // src/frontend/web_frontend.cpp:47-48
    html += <script>pageJs</script> (if pageJs)   // src/frontend/web_frontend.cpp:49
    html += </body></html>
    html.replace("__FWVER__", FIRMWARE_VERSION)   // src/frontend/web_frontend.cpp:51
    → HTTP 200, text/html, Cache-Control: no-cache
```

### 2.2 `sendRawPage` — Raw Pages (No Shared Layout)

Source: `src/frontend/web_frontend.cpp:57`

- Same `__FWVER__` replacement.
- No shared CSS/JS/navbar — each page is self-contained.
- Used for: setup, reset, OTA progress, config saved, setup done, reset done, OTA done.

## 3. Page Route Mapping

| Route | Handler | Method | Page Fragments | Source |
|---|---|---|---|---|
| `/` | `handleRoot` | GET | `sendAppPage(INDEX_PAGE_BODY, INDEX_PAGE_CSS, INDEX_PAGE_JS)` | `src/frontend/web_frontend.cpp:65-67` |
| `/config` | `handleConfigGet` | GET | `sendAppPage(CONFIG_PAGE_BODY, CONFIG_PAGE_CSS, CONFIG_PAGE_JS)` | `src/frontend/web_frontend.cpp:69-71` |
| `/rdm` | `handleRdmPage` | GET | `sendAppPage(RDM_PAGE_BODY, RDM_PAGE_CSS, RDM_PAGE_JS)` | `src/frontend/web_frontend.cpp:73-75` |
| `/setup` | `handleSetupGet` | GET | `sendRawPage(SETUP_PAGE)` | `src/frontend/web_frontend.cpp:77-78` |
| `/reset` | `handleResetGet` | GET | `sendRawPage(RESET_PAGE)` | `src/frontend/web_frontend.cpp:81-82` |
| `/ota` | `handleOtaStatus` | GET | `sendRawPage(OTA_PROGRESS_PAGE)` | `src/frontend/web_frontend.cpp:85-86` |
| `/config/saved` | `handleConfigSaved` | GET | `sendRawPage(CONFIG_SAVED_PAGE)` | `src/frontend/web_frontend.cpp:89-90` |
| `/setup/done` | `handleSetupDone` | GET | `sendRawPage(SETUP_DONE_PAGE)` | `src/frontend/web_frontend.cpp:93-94` |
| `/reset/done` | `handleResetDone` | GET | `sendRawPage(RESET_DONE_PAGE)` | `src/frontend/web_frontend.cpp:97-98` |

## 4. Navbar Stats Decoder

Located in `src/frontend/base/navbar.h:51-86`, function `stats(buf)`.

### 4.1 Binary Frame Parsing

```javascript
function stats(buf) {
    var v = new DataView(buf);
    var nOut = Math.floor((buf.byteLength - 16 - 10 - 1) / (512 + 5));  // src/frontend/base/navbar.h:54
    var statsOff = 16 + nOut * 512;
    // per-output TX fps at statsOff + 0..7 (2 bytes each)
    // per-output RX fps at statsOff + 8..15 (2 bytes each)
    // TX style at statsOff + 16..19 (1 byte each)
    // header: fps at 0, rssi at 2, heap at 4, uptime at 8, senders at 12, srcStatus at 13, jitter at 14
    // nav tail: fixtures at byteLength-10, rdmTx at byteLength-8, rdmRx at byteLength-4
}
```

### 4.2 TX Style Decoding

Source: `src/frontend/base/navbar.h:66-69`

```javascript
se.textContent = sty.map(function(b) {
    return (b & 1 ? 'D' : 'C') + (b & 2 ? '·' : '');
}).join(' ');
```
- Bit 0 (`0x01`): `D` = Delta, `C` = Continuous
- Bit 1 (`0x02`): `·` = style set over Art-Net

### 4.3 RSSI / Link Display

Source: `src/frontend/base/navbar.h:75-77`

| Condition | Display | Color |
|---|---|---|
| `rssi >= 10` | "LAN" + Mbps | Green (`#45d85c`) |
| `rssi >= 1` | "AP" + "active" | Magenta (`#f33abc`) |
| else | "WiFi" + dBm | Green (> -65), Amber (> -80), Magenta (else) |

## 5. Index Page — Status View (`src/frontend/scripts/index_js.h`)

### 5.1 WebSocket Connection

Source: `src/frontend/scripts/index_js.h:227-271`

```javascript
sock = new WebSocket('ws://' + location.host + '/ws');  // src/frontend/scripts/index_js.h:228
sock.binaryType = 'arraybuffer';
sock.onopen  = function() { badge: "Live" green; }       // src/frontend/scripts/index_js.h:231
sock.onclose = function() { badge: "Offline" magenta; reconnect in 2s; }  // src/frontend/scripts/index_js.h:233
sock.onmessage = function(e) {                          // src/frontend/scripts/index_js.h:234
    if (typeof e.data === 'string') {
        // Text frame: meta push (senders + log)
        var meta = JSON.parse(e.data);
        if (meta.meta) updateSenders(meta.senders, updateLog(meta.log));
    }
    if (e.data instanceof ArrayBuffer) {
        LuxNav.stats(e.data);  // navbar decode
        applyDmx(new Uint8Array(e.data, 16 + viewOut * 512, 512));  // src/frontend/scripts/index_js.h:268
    }
};
```

### 5.2 Channel Grid

- 512 cells built at page load (`src/frontend/scripts/index_js.h:66-77`).
- Each cell: channel number, value, gauge bar, optional label.
- Cell color: `cellBg(v)` dark-slate-to-cyan gradient (`src/frontend/scripts/index_js.h:150`).
- Click opens modal at `src/frontend/scripts/index_js.h:174`.

### 5.3 Channel Modal

- Slider (0-255) sends `{"type":"set","ch":N,"val":V}` (`src/frontend/scripts/index_js.h:198`).
- Quick buttons: Off (0), 50% (128), Full (255) (`src/frontend/scripts/index_js.h:200`).
- Identify button sends `{"type":"identify","ch":N}` (`src/frontend/scripts/index_js.h:147`).
- Sparkline: 60 samples at 500 ms = 30s history (`src/frontend/scripts/index_js.h:206-224`).

### 5.4 Output Selector

- Built from `/info.json` `outputs[]`.
- Buttons: "Output A · U0 · fps fps" format (`src/frontend/scripts/index_js.h:39-41`).
- Switching output sends `{"type":"viewout","out":idx}` (`src/frontend/scripts/index_js.h:22`).

### 5.5 Sender Table

- Populated from WebSocket meta push (text frames) (`src/frontend/scripts/index_js.h:238-240`).
- No longer polls `/senders.json` (`src/frontend/scripts/index_js.h:318-321`).
- Renders: IP, Protocol, FPS, Last seen (`src/frontend/scripts/index_js.h:276-291`).

### 5.6 Change Log

- Populated from WebSocket meta push (`src/frontend/scripts/index_js.h:238-240`).
- Renders: timestamp, protocol, channel changes (`src/frontend/scripts/index_js.h:293-316`).

### 5.7 Firmware Update Banner

- Fetches `/version.json` then `https://luxdmx.org/firmware/releases` (`src/frontend/scripts/index_js.h:360`).
- Compares versions via `vNum()` numeric encoding (`src/frontend/scripts/index_js.h:5,324,394`).
- Aggregates changelog from newer releases (`src/frontend/scripts/index_js.h:393-404`).
- Install button submits `POST /ota/github` with version (`src/frontend/scripts/index_js.h:386-390,410-411`).

## 6. Config Page — Settings View (`src/frontend/scripts/config_js.h`)

### 6.1 Initialization

- Fetches `/info.json` and populates all form fields (`src/frontend/scripts/index_js.h:155`... actually `src/frontend/scripts/config_js.h:155`).
- On error: renders skeleton with disabled save button (`src/frontend/scripts/config_js.h:235-255`).

### 6.2 Output Configuration

- Template-based: `<template id="out-tpl">` cloned per output (`src/frontend/scripts/config_js.h:32`).
- Fields per output: enabled, universe, UART port, TX pin, RX pin, RDM RTS pin, merge mode, loss mode, style, rate (`src/frontend/scripts/config_js.h:37-61`).
- Letter prefix per output index: `a_`, `b_`, `c_`, `d_` (`src/frontend/scripts/config_js.h:13,50`).

### 6.3 Wired Ethernet Selection

- Single `<select id="wired-sel">` drives all Ethernet fields (`src/frontend/scripts/config_js.h:97-105`).
- Options: "None", "W5500", "DM9051", "RMII PHY N" (`src/frontend/scripts/config_js.h:99-103`).
- `updWired()` maps the single selection to hidden firmware fields (`src/frontend/scripts/config_js.h:111-123`).
- RMII PHY labels: `['LAN8720 / LAN8742', 'IP101', 'RTL8201', 'DP83848', 'KSZ8081', 'JL1101']` (`src/frontend/scripts/config_js.h:93`).

### 6.4 Network Mode

- WiFi vs wired → hides/shows relevant fields (`src/frontend/scripts/config_js.h:127-140`).
- AP mode → shows AP password field (`src/frontend/scripts/config_js.h:135`).
- Static IP → shows static IP fields (`src/frontend/scripts/config_js.h:137`).

### 6.5 LED Configuration

- Type 1/2: simple LED (shows pin selector) (`src/frontend/scripts/config_js.h:141-143`).
- Type 3: 5-LED RGBAW panel (shows 5 individual pin selectors + brightness) (`src/frontend/scripts/config_js.h:144`).

### 6.6 Display Configuration

- Types 1-3: I2C (SDA/SCL) (`src/frontend/scripts/config_js.h:147-148`).
- Type 4: SPI (CS/DC/RST/SCK/MOSI) (`src/frontend/scripts/config_js.h:149`).

### 6.7 Board Diagram & Pin Picker

- Built-in board descriptors with physical header layouts (`src/frontend/scripts/config_js.h:602-810`).
- Hardwired pins locked per board (e.g., LuxDMX v6: `a_tx=GPIO17`, `a_rx=GPIO18`, etc.) (`src/frontend/scripts/config_js.h:715-735`).
- Pin picker opens a modal showing the board diagram (`src/frontend/scripts/config_js.h:884-900`).

### 6.8 Pin Validation

- `activeRoles()` returns all currently configurable GPIO roles (`src/frontend/scripts/config_js.h:855-867`).
- `pinFlags(g)` looks up board-specific pin flags (flash, serial, input-only, strapping, usb-jtag, reserved) (`src/frontend/scripts/config_js.h:920`).
- Conflict detection: shows warnings when two roles claim the same pin.

### 6.9 Collapsible Sections

- Every card folds open/closed via header click (`src/frontend/scripts/config_js.h:510-534`).
- State saved in `localStorage` under key `lux_cfg_sections` (`src/frontend/scripts/config_js.h:471-474`).
- "Expand all" / "Collapse all" remembers bulk state separately (`src/frontend/scripts/config_js.h:497-501`).
- Section summaries computed via `SEC_SUM` table (`src/frontend/scripts/config_js.h:541-584`).

### 6.10 Form Submission

- Submit intercepted → `fetch('POST /config')` with FormData (`src/frontend/scripts/config_js.h:310`).
- Client-side validation: prevents saving enabled output without a TX pin (`src/frontend/scripts/config_js.h:286-303`).
- On response: if `reboot=true`, shows modal listing which fields require restart (`src/frontend/scripts/config_js.h:313-321`).
- Save button disabled until `/info.json` loads successfully (`src/frontend/scripts/config_js.h:231`).

## 7. RDM Page — Fixtures View (`src/frontend/scripts/rdm_js.h`)

### 7.1 WebSocket Connection

- Separate socket from the index page (`src/frontend/scripts/rdm_js.h:6,13-19`).
- Same `/ws` endpoint, binary + text frames.
- `sock.onmessage` only feeds `LuxNav.stats(e.data)` to the shared navbar (`src/frontend/scripts/rdm_js.h:17`).

### 7.2 Fixtures Table

- Columns: Name, UID, Uni, Addr, Foot, Persona, Mfg, Model, Category, Sensors, Identify button (`src/frontend/scripts/rdm_js.h:187-188`).
- Sorting by any column header click (`src/frontend/scripts/rdm_js.h:210-213`).
- Expand/collapse rows for per-fixture editing (`src/frontend/scripts/rdm_js.h:240`).

### 7.3 Sensor Charts

- SVG sparkline charts rendered inline (320×96) (`src/frontend/scripts/rdm_js.h:75-104`).
- 60-sample rolling window (~3 minutes of history) (`src/frontend/scripts/rdm_js.h:36-38`).
- History persisted in `localStorage` under key `rdmhist` (`src/frontend/scripts/rdm_js.h:43-59`).
- Sensors grouped by type (Temperature, Voltage, Current, etc.) with shared axes (`src/frontend/scripts/rdm_js.h:60-74`).

### 7.4 RDM Commands

All sent as WebSocket text frames:

| Action | WebSocket Message | Source |
|---|---|---|
| Discover | `{type:'rdm_discover',line:N}` | `src/frontend/scripts/rdm_js.h:332` |
| Set Address | `{type:'rdm_setaddr',uid:...,addr:N}` | `src/frontend/scripts/rdm_js.h:334-335` |
| Set Label | `{type:'rdm_setlabel',uid:...,label:...}` | `src/frontend/scripts/rdm_js.h:336` |
| Set Personality | `{type:'rdm_setpers',uid:...,pers:N}` | `src/frontend/scripts/rdm_js.h:337` |
| Identify | `{type:'rdm_identify',uid:...,on:true/false}` | `src/frontend/scripts/rdm_js.h:338` |
| Sensor toggle | `{type:'rdm_sensorsel',uid:...,sensor:N,on:bool}` | `src/frontend/scripts/rdm_js.h:358` |

- HTTP fallbacks for merge mode and BQP: `fetch('/rdm/merge?out=N&mode=M')` and `fetch('/rdm/bqp?p=N)` (`src/frontend/scripts/rdm_js.h:339-340`).
- Polling: 600ms during discovery, 1s with live sensors, 3s otherwise (`src/frontend/scripts/rdm_js.h:299-304`).

### 7.5 RDM Categories

Standard ANSI E1.20 device categories (`src/frontend/scripts/rdm_js.h:22`):
- 0x0100: Fixture, 0x0101: Fixture Fixed, 0x0102: Fixture Moving Yoke, 0x0103: Fixture Moving Mirror
- 0x0200: Fixture Accessory, 0x0500: Dimmer, 0x0600: Power, 0x0700: Scenic
- 0x0800: Data, 0x0a00: Monitor, 0x7000: Control, 0x7100: Test

## 8. Shared JavaScript (`src/frontend/scripts/shared_js.h`)

### 8.1 Utility Functions

| Function | Purpose | Source |
|---|---|---|
| `esc(s)` | HTML-escape a string | `src/frontend/scripts/shared_js.h:5` |
| `vNum(s)` | Convert "1.2.3" to numeric for comparison | `src/frontend/scripts/shared_js.h:6` |
| `extractChanges(body)` | Parse GitHub release notes into change list | `src/frontend/scripts/shared_js.h:7-14` |
| `showModal(opts)` | Promise-based confirm dialog | `src/frontend/scripts/shared_js.h:15-38` |

### 8.2 App Modal

- `#app-modal` element with title, body, OK/Cancel buttons (`src/frontend/scripts/shared_js.h:42-55`).
- ESC key cancels, Enter confirms (`src/frontend/scripts/shared_js.h:32`).
- Click outside modal closes (`src/frontend/scripts/shared_js.h:35`).

## 9. Memory & Performance Model

- All HTML/CSS/JS fragments stored as `PROGMEM` strings (`src/frontend/web_frontend.cpp:26` uses `FPSTR()` macros).
- `sendAppPage` reserves 20 KB upfront (`src/frontend/web_frontend.cpp:28`).
- WebSocket meta push: 2 Hz text frame (senders + log) via `wsPushMeta()` (`src/net/websocket.cpp:33`).
- WebSocket binary push: 10 Hz frame via `wsPush()` (`src/net/websocket.cpp:14`).
- `canSend()` check before binary push (`src/net/websocket.cpp:28`).
- Binary frame size: 2095 bytes (`src/net/ws_frame.h:13`).

## 10. Cache Strategy

| Resource | Cache-Control | Source |
|---|---|---|
| HTML pages | `no-cache` | `src/frontend/web_frontend.cpp:53,61` |
| Logo (webp) | `max-age=604800` | `src/net/web_pages.cpp:12` |
| Favicon (png) | `max-age=604800` | `src/net/web_pages.cpp:18` |
| Bootstrap CSS | `max-age=604800` + `Content-Encoding: gzip` | `src/net/web_pages.cpp:24-25` |
| Firmware version | `?v=__FWVER__` query param (bust on version change) | `src/frontend/web_frontend.cpp:32-33` |

## 11. Navigation

Navbar links (defined in `NAVBAR_HTML`, `src/frontend/base/navbar.h:36-39`):
- `/` → Status page
- `/rdm` → RDM fixtures page
- `/config` → Settings page

## 12. Cross-Module Integration

| Integration | Source |
|---|---|
| WebSocket push (index.js) receives binary frames | [WebSocket Protocol](net-websocket-protocol.md) |
| `/info.json` consumed by config.js for field population | [Web Routes](net-web-routes.md) `handleInfoJson` |
| `/rdm.json` consumed by rdm.js for fixture list | [Web Routes](net-web-routes.md) `handleRdmJson` |
| `/version.json` consumed by index.js for update checks | [Web Routes](net-web-routes.md) `handleVersionJson` |
| `/labels.json` + POST /labels consumed by index.js | [Web Routes](net-web-routes.md) `handleLabelsGet/Body` |
| `/ota/github` POST consumed by index.js OTA form | [Web Routes](net-web-routes.md) `handleOtaGithub` |
| `/autoupdate` POST consumed by config.js toggle | [Web Routes](net-web-routes.md) `handleAutoUpdatePost` |

## 13. Firmware Version Injection

- `__FWVER__` placeholder in all HTML/CSS/JS fragment strings (`src/frontend/web_frontend.cpp:51`).
- Replaced with `FIRMWARE_VERSION` at serve time (`src/frontend/web_frontend.cpp:32,51`).
- `FIRMWARE_VERSION` macro from `src/sys/firmware_version.h` (auto-generated by `extra_scripts.py`).

## 14. Error Handling (Browser-Side)

- WebSocket disconnects auto-reconnect after 2 s (`src/frontend/scripts/index_js.h:233`, `src/frontend/scripts/rdm_js.h:16`).
- `/info.json` fetch failure: renders skeleton, disables save button, shows error modal (`src/frontend/scripts/config_js.h:235-255`).
- `/rdm.json` fetch failure: silently caught, schedule continues (`src/frontend/scripts/rdm_js.h:305`).
- OTA release fetch failure: shows "Could not reach LuxDMX.org" (`src/frontend/scripts/index_js.h:455-456`).
- Form submission failure: restores button state, shows "device did not report back" modal (`src/frontend/scripts/config_js.h:328-333`).

## 15. Testing

- No native host tests for frontend code.
- Playwright E2E tests exercise the browser-side JavaScript (`docs/e2e/`, not yet in repo).
- The `LuxNav.stats()` function in `src/frontend/base/navbar.h:51` is a runtime contract — if the binary frame layout changes, the navbar decode breaks.

## 16. Known Limitations

- `logJson()` on the server returns `"[]"` (empty stub) at `src/net/web_routes.cpp:551-558` — the change log table only populates if the server-side log JSON is implemented.
- `handleLabelsGet` returns `"{}"` (stub) at `src/net/web_routes.cpp:292` — labels are managed client-side in `localStorage`, not persisted on the device.
- RDM page maintains a separate WebSocket connection from the index page — both connect to the same `/ws` endpoint.
- The 20 KB `html.reserve(20000)` (`src/frontend/web_frontend.cpp:28`) is a heuristic — oversized pages could still cause heap fragmentation.

## 17. Build Integration

- Frontend headers are included by `src/net/web_server.cpp` (which includes `web_pages.h` and `frontend/web_frontend.h`) at `src/net/web_server.cpp:3-4`.
- Route wiring: `webRegisterRoutes()` at `src/net/web_server.cpp:29` references all handlers.
- `wsInit(http)` called after routes at `src/main.cpp:126`.

## 18. Related Environment Configuration

- AsyncTCP core pinning: [`platformio.ini:42`](platformio.ini#L42) (`CONFIG_ASYNC_TCP_RUNNING_CORE=0`)
- AsyncTCP stack size: [`platformio.ini:40`](platformio.ini#L40) (16 KB)

## 19. File Dependency Graph

```
src/main.cpp:125
    ↓ webRegisterRoutes()  src/net/web_server.cpp:93
        ↓ webRegisterRoutes(http)  src/net/web_server.cpp:29
            ├── handleRoot → sendAppPage  src/frontend/web_frontend.cpp:65
            │   ├── FRONTEND_STYLES  src/frontend/base/styles.h:4
            │   ├── NAVBAR_CSS/HTML/JS  src/frontend/base/navbar.h:4,18,43
            │   ├── INDEX_PAGE_BODY/CSS/JS  src/frontend/pages/index_page.h:4, index_css.h, scripts/index_js.h:4
            │   ├── FOOTER_HTML  src/frontend/base/footer.h:4
            │   └── APP_MODAL_HTML + SHARED_JS  src/frontend/scripts/shared_js.h:42,4
            ├── handleConfigGet → sendAppPage
            │   └── CONFIG_PAGE_BODY/CSS/JS  src/frontend/pages/config_page.h, config_css.h, scripts/config_js.h:4
            ├── handleRdmPage → sendAppPage
            │   └── RDM_PAGE_BODY/CSS/JS  src/frontend/pages/rdm_page.h, rdm_css.h, scripts/rdm_js.h:4
            ├── ... static assets, JSON routes, OTA, RDM triggers, setup, reset ...
    ↓ wsInit(http)  src/main.cpp:126
        └── src/net/websocket.cpp:47
```

## 20. Frontend-to-Backend API Contract

### 20.1 `/info.json` Response Fields (consumed by `config_js.h`)

| Field | Type | Source in `handleInfoJson` |
|---|---|---|
| `version` | string | `src/sys/firmware_version.h` + `src/net/web_routes.cpp:88` |
| `board` / `mcu` | string | `BOARD_ID` / `MCU_ID` (`src/sys/sys_platform.h:11-12`) |
| `boardSel` | string | `cfg.boardSel` |
| `outputs[]` | array | `cfg.outputs` via `cfgcore::exportJson` |
| `protocol` (0=ArtNet, 1=sACN, 2=both) | int | `cfg.protocol` |
| `ethSpi` / `ethRmii` / `hasEth` | bool | Platform defines |
| `wiredPhy` / `rmiiPhy` / `ethW5500` / `ethSpiPhy` | int/bool | `cfg` fields |
| `apPassword` / `autoUpdate` / `otapw` | string/bool | `cfg` fields |
| `wifiMode` / `wifiSsid` / `staticIp` / `ip` / `gateway` / `subnet` / `dns` | various | `cfg` fields |
| `ledType` / `ledPin` / `ledR/G/Y/B/W` | int | `cfg` fields |
| `dispType` / `dispSda` / `dispScl` / ... | int | `cfg` fields |
| `encA`/`encB`/`encSw`/`encSteps`/`encReverse` | int/bool | `cfg` fields |
| `btn1Pin`...`btn4Pin`, `btn1Act`...`btn4Act`, `btnActiveHigh` | int/bool | `cfg` fields |
| `ethCs`/`ethSck`/`ethMosi`/`ethMiso`/`ethInt`/`ethRst`/`ethFreq` | int | `cfg` fields |
| `rmiiClk`/`rmiiAddr`/`rmiiMdc`/`rmiiMdio`/`rmiiPwr` | int | `cfg` fields |

### 20.2 WebSocket Binary Frame (consumed by `navbar.h` + `index_js.h`)

See [WebSocket Protocol](net-websocket-protocol.md) for the full 2095-byte layout.

### 20.3 WebSocket Text Frame (meta push)

```json
{"meta":1,"senders":[...],"log":[...]}
```
- Sent by `wsPushMeta()` at `src/net/websocket.cpp:38-42`.
- `sendersJson()` at `src/net/web_routes.cpp:532`.
- `logJson()` at `src/net/web_routes.cpp:551` (currently returns `"[]"`).

### 20.4 WebSocket Text Command (browser → ESP32)

| Command | Fields | Handler |
|---|---|---|
| `subscribe` | `universes: [int]` | `src/net/ws_handler.cpp:147` |
| `viewout` | `out: int` | `src/net/ws_handler.cpp:171` |
| `blackout` | — | `src/net/ws_handler.cpp:179` |
| `mode` | `manual: bool` | `src/net/ws_handler.cpp:184` |
| `identify` | `ch: int` | `src/net/ws_handler.cpp:188` |
| `set` | `ch: int`, `val: int` | `src/net/ws_handler.cpp:197` |
| `scene` | `play: int`, `fade: int` | `src/net/ws_handler.cpp:209` |
| `saveScene` | `idx: int`, `name: string` | `src/net/ws_handler.cpp:220` |
| `clearScene` | `idx: int` | `src/net/ws_handler.cpp:242` |
| `rdm_discover` | `line: int` | `handleWsTextRdm` → `g_pendingAction=1` |
| `rdm_setaddr` | `uid: string`, `addr: int` | `handleWsTextRdm` → `g_pendingAction=2` |
| `rdm_identify` | `uid: string`, `on: bool` | `handleWsTextRdm` → `g_pendingAction=3` |
| `rdm_setpers` | `uid: string`, `pers: int` | `handleWsTextRdm` → `g_pendingAction=4` |
| `rdm_setlabel` | `uid: string`, `label: string` | `handleWsTextRdm` → `g_pendingAction=5` |

## 21. References

- Route registration: [`src/net/web_server.cpp:29-91`](src/net/web_server.cpp#L29)
- HTML assembly: [`src/frontend/web_frontend.cpp:26-55`](src/frontend/web_frontend.cpp#L26)
- Index page WebSocket: [`src/frontend/scripts/index_js.h:228`](src/frontend/scripts/index_js.h#L228)
- Navbar stats decoder: [`src/frontend/base/navbar.h:51`](src/frontend/base/navbar.h#L51)
- Frame layout constants: [`src/net/ws_frame.h:6-14`](src/net/ws_frame.h#L6)
- Config JS population: [`src/frontend/scripts/config_js.h:155`](src/frontend/scripts/config_js.h#L155)
- RDM page JS: [`src/frontend/scripts/rdm_js.h:1`](src/frontend/scripts/rdm_js.h#L1)
- Shared JS utilities: [`src/frontend/scripts/shared_js.h:4`](src/frontend/scripts/shared_js.h#L4)
- Firmware version: [`src/sys/firmware_version.h`](src/sys/firmware_version.h)
- WebSocket handler: [WebSocket Handler](net-ws-handler.md)
- WebSocket push: [WebSocket Protocol](net-websocket-protocol.md)
- Route handlers: [Web Routes](net-web-routes.md)
- Static assets: [Web Server](net-web-server.md)
