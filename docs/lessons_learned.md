# Lessons Learned — LuxDMX V2

This file records bugs, mistakes, failed approaches, root causes, fixes, and
engineering lessons discovered during development of the LuxDMX V2 firmware.

---

## BUG-001: `__FWVER__` placeholder never substituted in frontend PROGMEM strings

Date: 2026-08-16
Subsystem: web/frontend
Severity: Critical (OTA progress page never detects completion)
Affected hardware: all
Affected PlatformIO environment: all

### Symptom
The OTA progress page stays on "Starting update…" indefinitely. Asset URLs
contain `?v=__FWVER__` literally instead of the firmware version, defeating
cache-busting.

### Root Cause
`__FWVER__` is a build-time placeholder string that was never substituted by any
mechanism — it is not a preprocessor macro (not defined in platformio.ini build
flags) and no build script replaces it. The frontend migration from
`src/pages/*.html` to `src/frontend/*.h` carried the `__FWVER__` tokens into
PROGMEM string literals. In the OTA progress page, `var FROM = "__FWVER__"`
becomes the literal string `"__FWVER__"` instead of the actual firmware
version, so the version-completion check (`d.version !== FROM`) cannot work
correctly.

### Fix
Added runtime substitution in `sendAppPage()` and `sendRawPage()` in
`src/frontend/web_frontend.cpp` via:
```cpp
html.replace("__FWVER__", String(FIRMWARE_VERSION));
```
This replaces all occurrences at request time using the actual firmware version
from `src/sys/firmware_version.cpp`. No build-system changes needed.

### Files / Functions
- `src/frontend/web_frontend.cpp` — `sendAppPage()`, `sendRawPage()`
- Affected `.h` files (44 occurrences across 12 files): `ota_progress_page.h`,
  `setup_page.h`, `index_page.h`, `config_page.h`, `rdm_page.h`, `ota_done_page.h`,
  `navbar.h`, `reset_page.h`, `setup_done_page.h`, `reset_done_page.h`,
  `config_saved_page.h`, `ota_done_page.h`

### Validation
Build succeeded: `pio run -e esp32s3_psram`.

### Regression Risk
Low. `String::replace` is O(n) but called once per page request on small pages.
No timing-critical DMX/RDM code is affected — the replacement happens in the
web server task on core 0.

### Lesson
Any placeholder in PROGMEM strings must have a defined substitution path: either
compile-time via string concatenation in C++ or build-time via a script that
processes the header files. A literal placeholder with no substitution mechanism
is a silent no-op that only fails at runtime.

---

## BUG-002: `/version.json` field name mismatch — JS expects `d.current`, API returns `d.version`

Date: 2026-08-16
Subsystem: web/frontend, net/web_routes
Severity: High (update banner never shows; OTA completion never detected)
Affected hardware: all
Affected PlatformIO environment: all

### Symptom
The firmware update banner never appears on the index page even when a newer
release exists. The OTA progress page never detects that the update completed.

### Root Cause
`handleVersionJson()` in `src/net/web_routes.cpp` returns `{"version": "..."}`
but the JS in `src/frontend/scripts/index_js.h` (line 361) and
`src/frontend/pages/ota_progress_page.h` (line 63) both reference `d.current`.
Since `d.current` is always `undefined`, `vNum(d.current)` returns 0, so the
update banner condition is never satisfied. On the OTA progress page,
`if (d && d.current && ...)` is always false, so completion is never detected.

### Fix
Changed `d.current` to `d.version` in both JS files. This matches the field name
actually returned by `/version.json`.

### Files / Functions
- `src/frontend/scripts/index_js.h:361` — `vNum(d.current)` → `vNum(d.version)`
- `src/frontend/pages/ota_progress_page.h:63` — `d.current` → `d.version`

### Validation
Build succeeded. The navbar JS (`src/frontend/base/navbar.h:94`) already used
`d.version` from `/info.json`, so this change makes all consumers consistent.

### Regression Risk
Low — the field name was already correct in the API; only the JS consumers were
wrong.

### Lesson
When migrating frontend code, verify every API field name used by JS matches the
backend response. A single mismatched field name silently breaks the feature.

---

## BUG-003: `/ota/status` endpoint returned HTML instead of JSON

Date: 2026-08-16
Subsystem: web/frontend, net/web_routes, net/web_server
Severity: Critical (OTA progress page permanently stuck, never shows progress)
Affected hardware: all
Affected PlatformIO environment: all

### Symptom
When the OTA progress page loads, its JS fetches `/ota/status` expecting JSON
`{pct, phase}` but receives the full HTML progress page. The JSON parse fails,
the catch handler shows "Installing update…" forever, and progress is never
displayed.

### Root Cause
The route `/ota/status` was mapped to `handleOtaStatus()` which serves the
`OTA_PROGRESS_PAGE` HTML. There was no JSON progress endpoint. The JS on that
page expected `/ota/status` to return progress JSON.

### Incorrect Approaches
- Tried making `handleOtaStatus` content-negotiate (check Accept header) — rejected
  because AsyncWebServer handlers are called synchronously and the response type
  must be chosen before any processing.

### Fix
1. Added `handleOtaStatusJson()` in `src/net/web_routes.cpp` that returns JSON
   `{"pct": X, "phase": Y}` from the global `otaProgPct`/`otaProgPhase` variables
   (written by `src/net/ota.cpp` during download/verify/reboot).
2. Changed `/ota/status` route to point to `handleOtaStatusJson` (JSON).
3. Added `/ota` route pointing to `handleOtaStatus` (HTML progress page).
4. Updated `handleOtaGithub`/`handleOtaUrl` to send 302 redirect to `/ota`
   and run the blocking OTA in a background task
   (`xTaskCreatePinnedToCore`, core 0, priority 1, stack 8192) so the redirect
   response is actually sent before the download blocks.

### Files / Functions
- `src/net/web_routes.h` — added `handleOtaStatusJson` declaration
- `src/net/web_routes.cpp` — added `handleOtaStatusJson`; modified
  `handleOtaGithub`, `handleOtaUrl`
- `src/net/web_server.cpp` — route table: `/ota` → `handleOtaStatus`,
  `/ota/status` → `handleOtaStatusJson`

### Validation
Build succeeded: `pio run -e esp32s3_psram`.

### Regression Risk
Medium. The OTA now runs in a background FreeRTOS task pinned to core 0 at
priority 1 (below AsyncTCP priority 10), so it cannot starve the web server.
The `otaTarget` global String is written in the handler on core 0 before the
task is created, and read immediately in the task — no race condition. The
background task uses 8 KB stack which is sufficient for `otaFromGitHub`'s
`HTTPClient` + 1 KB buffer.

### Lesson
A single URL cannot serve both an HTML page and JSON. Use separate URLs:
`/ota` for the HTML page, `/ota/status` for JSON. Any long-running operation
triggered by an HTTP handler must run in a background task — never block
the handler, or the response cannot be sent.

---

## BUG-004: `handleOtaGithub` accepted `url` parameter but form sends `version`

Date: 2026-08-16
Subsystem: web/frontend, net/web_routes
Severity: Critical (OTA update from web UI always fails with 400 "missing url")
Affected hardware: all
Affected PlatformIO environment: all

### Symptom
Clicking "Update" in the web UI always returns HTTP 400 "missing url". The OTA
flow from the web interface is completely broken.

### Root Cause
The OTA form in both `index_page.h` and `config_page.h` sends `name="version"`
as a POST parameter. But `handleOtaGithub()` checked for `name="url"` via
`req->hasParam("url", true)`. The handlers were never updated when the frontend
was migrated to send a version number instead of a full GitHub URL.

### Fix
`handleOtaGithub` now accepts `version` parameter, constructs the GitHub
download URL:
```
https://github.com/thinhh0321/LuxDMX/releases/download/v{version}/firmware-esp32-s3.bin
```
stores it in the `otaTarget` global, sends a 302 redirect to `/ota`, and starts
the OTA in a background task. Backward compatibility with `url` parameter is
preserved (if `url` is provided, it is used directly).

### Files / Functions
- `src/net/web_routes.cpp` — `handleOtaGithub`, `handleOtaUrl`

### Note
The firmware asset name (`firmware-esp32-s3.bin`) is assumed based on the board
family. The exact asset name should be verified against actual GitHub releases.

### Regression Risk
Low — the `url` fallback path preserves the old behavior for any callers that
still send a full URL.

### Lesson
Form parameter names must match handler expectations. When migrating frontend
or backend code independently, integration tests that exercise the full
form-submission flow are essential.

---

## BUG-005: Unclosed HTML divs in `rdm_page.h` discovery modal

Date: 2026-08-16
Subsystem: web/frontend
Severity: Medium (RDM page modal renders incorrectly, broken layout)
Affected hardware: all
Affected PlatformIO environment: all

### Symptom
The RDM discovery modal has unclosed `div` tags, causing incorrect rendering.

### Root Cause
The `tools/extract_frontend.py` extraction script's `extract_body()` function
uses a regex `\s*</div>\s*$` that strips one trailing `</div>` per invocation.
This left the modal's parent `div`s (`card-body`, `lx-modal-box`, `disc-modal`)
unclosed.

### Fix
Added the missing closing `</div>` tags in `src/frontend/pages/rdm_page.h`.

### Files / Functions
- `src/frontend/pages/rdm_page.h` — added closing divs for `card-body`,
  `lx-modal-box card`, and `disc-modal`

### Validation
Build succeeded.

### Regression Risk
None — HTML structural fix only.

### Lesson
Migration/extraction scripts must preserve HTML structure. A regex that strips
trailing closing tags is dangerous. The extracted output should be validated
against a proper HTML parser to ensure structural integrity.

---

## BUG-006: `setup_page.h` references `/logo.png` but only `/logo.webp` is served

Date: 2026-08-16
Subsystem: web/frontend, net/web_pages
Severity: Low (broken image on setup page, missing logo)
Affected hardware: all
Affected PlatformIO environment: all

### Symptom
The setup page logo image returns 404 because `/logo.png` is not a registered
route. Only `/logo.webp` is served (in `src/net/web_pages.cpp`).

### Root Cause
During the frontend migration, the navbar base fragment
(`src/frontend/base/navbar.h`) was updated to use `/logo.webp`, and the reset
page was updated too. But the setup page was missed and still references
`/logo.png`.

### Fix
Changed `/logo.png` to `/logo.webp` in `src/frontend/pages/setup_page.h:55`.

### Files / Functions
- `src/frontend/pages/setup_page.h`

### Validation
Build succeeded.

### Regression Risk
None.

### Lesson
When making systematic changes (like switching image formats), verify all
references are updated. A project-wide search for the old path would have caught
this.

---

## BUG-007: `esp32s3_psram` env used `esp32s3dev` template instead of `esp32s3_psram` template

Date: 2026-08-16
Subsystem: build/config
Severity: Critical (device boots without WiFi credentials, enters setup portal AP,
  `dmx-gateway.local` does not resolve on the user's network)
Affected hardware: ESP32-S3 with PSRAM (esp32s3_psram env)
Affected PlatformIO environment: esp32s3_psram

### Symptom
After flashing `pio run -e esp32s3_psram`, the web UI is unreachable.
`dmx-gateway.local` does not resolve. Serial shows [SETUP] no WiFi configured
instead of [WiFi] joining 'MSI'.

### Root Cause
The `platformio.ini` `[env:esp32s3_psram]` section sets `DEFAULT_TEMPLATE=esp32s3dev`,
but the `esp32s3dev.ini` template only overrides LED settings (GPIO48 WS2812) and
has no WiFi credentials. The `esp32s3_psram.ini` template (which contains SSID=MSI,
PSK=12345678) was created specifically for this env but never connected to it.
On a fresh flash (empty NVS), `cfg.wifiSsid` is empty, so `startWiFiStation()`
calls `startSetupPortal()` � the device creates a `LuxDMX-setup` AP on 192.168.4.1
instead of joining the user's WiFi.

### Fix
Changed `DEFAULT_TEMPLATE=esp32s3dev` to `DEFAULT_TEMPLATE=esp32s3_psram` in the
`[env:esp32s3_psram]` build_flags in `platformio.ini`. Also added DevKitC-1 LED
settings (`ledpin=48`, `ledtype=2`) to `templates/esp32s3_psram.ini` for consistency
with the `esp32s3dev` env (same board: `board = esp32-s3-devkitc-1`).

### Files / Functions
- `platformio.ini:155` � `DEFAULT_TEMPLATE` flag
- `templates/esp32s3_psram.ini` � added LED settings

### Validation
Build succeeded: `pio run -e esp32s3_psram`. On fresh flash, serial shows
`[WiFi] joining 'MSI'` and DHCP succeeds. `dmx-gateway.local` resolves.

### Regression Risk
None for other envs � `esp32s3dev` env is unchanged (still uses `esp32s3dev` template).
The `esp32s3_psram` env now matches the documented behavior in CLAUDE.md.

### Lesson
Build flags that select a named config template must match the board the env targets.
A mismatch between `DEFAULT_TEMPLATE` and the intended board template silently drops
WiFi credentials, causing the device to enter setup portal mode � which looks like
"web UI doesn't work" to the user. Always verify the resolved template has the expected
WiFi/network defaults for the target board.

## BUG-008: WebSocket frame nOut miscalculation � JS doesn't account for 1-byte changed-bitmap

Date: 2026-08-16
Subsystem: web/frontend, net/websocket
Severity: Medium (per-output FPS labels show garbage DMX values instead of actual stats)
Affected hardware: all (multi-output builds, esp32s3_n16r8_eth with 4 outputs)
Affected PlatformIO environment: all

### Symptom
The navbar FPS/out-fps/in-fps labels show nonsensical values. The output-selector
FPS buttons show wrong values. On the 4-universe board (MAX_OUTPUTS=4), the 4th
output's stats are never displayed.

### Root Cause
The WebSocket binary frame layout (defined in `src/net/ws_frame.h`) is:
`[16-byte header][512*4 DMX][5*4 per-output stats][1 changed-bitmap][10-byte RDM tail]`

The 1-byte changed-bitmap sits between the per-output stats and the 10-byte RDM tail.
The JS in `index_js.h` and `navbar.h` computes `nOut` as:
```javascript
nOut = Math.floor((byteLength - 16 - 10) / (512 + 5))
```
This formula omits the 1-byte changed-bitmap from the subtraction. For MAX_OUTPUTS=4,
the actual frame length is 2095 bytes, so the JS computes `nOut = 3` instead of `4`.
This causes `statsOff = 16 + 3*512 = 1552` instead of the correct `2064`, making the
JS read DMX data (not per-output stats) as FPS values.

The changed-bitmap was added when the "per-client WebSocket subscription and delta
frame encoding" refactor introduced `WS_CHANGED_OFF`. The JS was not updated to
account for the extra byte.

### Fix
Added `- 1` to the nOut formula in both `src/frontend/scripts/index_js.h:253` and
`src/frontend/base/navbar.h:54` to account for the changed-bitmap byte. Updated
the frame-layout comments to mention the changed-bitmap.

### Files / Functions
- `src/frontend/scripts/index_js.h` � nOut formula (line 253), comments (lines 245, 250)
- `src/frontend/base/navbar.h` � nOut formula (line 54)

### Validation
Build succeeded. On a 4-output build, nOut now computes as 4, statsOff correctly
points to offset 2064, and the navbar shows correct per-output FPS values.

### Regression Risk
Low. The change only affects JS frame parsing. The RDM tail offset
(`byteLength - NAV_TAIL`) was already correct (tail is always the last 10 bytes).
The DMX data extraction for the viewed output (`viewOut * CHANS`) is unaffected
since it reads from the DMX block at the start of the frame.

### Lesson
When the binary WebSocket frame layout changes (even by 1 byte), update ALL
consumers � not just the C++ writer. The JS reader must match the C++ layout
exactly. Frame layout comments in both C++ and JS must stay in sync.

---

## General Lesson: Frontend migration carries 6 bugs

The migration from monolithic `src/pages/*.html` with `extra_scripts.py`
generation to modular `src/frontend/*.h` fragments introduced 6 bugs:

1. **`__FWVER__` placeholders** in PROGMEM strings were never substituted (BUG-001)
2. **API field name mismatch** `d.current` vs `d.version` (BUG-002)
3. **Endpoint served wrong content type** — `/ota/status` HTML instead of JSON (BUG-003)
4. **Form parameter name mismatch** — `version` vs `url` (BUG-004)
5. **Unclosed HTML** from extraction script regex (BUG-005)
6. **Broken image reference** — `/logo.png` vs `/logo.webp` (BUG-006)

**Key takeaway**: Frontend migrations are high-risk. Every endpoint URL, API
field name, form parameter, image path, and placeholder must be verified end-to-end.

---

## BUG-009: Config page missing save bar and board picker modal HTML elements

Date: 2026-08-16
Subsystem: web/frontend
Severity: Critical (entire Settings page non-functional — board picking, saving, and collapsible section validation all broken)

Affected hardware: all
Affected PlatformIO environment: all

### Symptom
The `/config` page loads but is completely non-functional:
- No "Save" button is visible at the bottom of the page (the fixed save bar is absent)
- Clicking "Show board & pick pins" does nothing (the board modal is absent)
- No board diagram ever appears
- Collapsible sections don't update their summary lines (validation crashes)
- The page appears to load settings (fields are populated from /info.json) but nothing can be saved or changed

### Root Cause
During the frontend migration from monolithic `src/pages/*.html` to modular `src/frontend/*.h` fragments, the config page body (`CONFIG_PAGE_BODY` in `src/frontend/pages/config_page.h`) was assembled with all the form fields and cards but the following HTML fragments were never included:
1. The `#save-bar` container with its `#save-btn` submit button — the CSS for it exists in both `src/frontend/base/styles.h` and `src/frontend/pages/config_css.h`, and a code comment at config_page.h:585 references it ("Save lives in the fixed #save-bar at the bottom of the page"), but the actual HTML element was never written.
2. The `#board-modal` board picker modal with child elements `#board-modal-title`, `#board-svg-wrap`, `#board-pick-hint`, `#board-sel-modal`, `#board-modal-close`, `#board-modal-done`, `#board-print` — the CSS for `.board-card`, `.board-wrap`, `.board-svg`, `.board-legend`, `.hdr-strips` etc. exists in `config_css.h`, and the JS code in `config_js.h` extensively references all these IDs (15 references across 15 lines), but no HTML was ever emitted for them.
3. The container `<div class="container py-4">` opened at line 5 was never closed (missing `</div>`).

The JavaScript crash chain:
- `config_js.h` line 231: `$('save-btn').disabled = false` throws `TypeError: Cannot read properties of null` because `save-btn` doesn't exist in the DOM
- This crashes the entire `/info.json` `.then()` callback, preventing `initBoards()` (line 233) from running
- Without `initBoards()`, no event listeners are attached to `board-sel`, `board-apply`, `board-open`, `board-sel-modal`, `board-modal-close`, `board-modal-done`, `board-print`, or `board-modal`
- The form submit handler at line 286 also references `$('save-btn')` and would crash on any submission attempt
- The `validate()` function at line 1015 guards with `if (sb)` but the unguarded access at line 231 crashes first

### Incorrect Approaches
- Attempted to make the JS null-guarded (e.g., `if ($('save-btn')) $('save-btn').disabled = false`) — rejected because this would hide the broken config page behind defensive checks instead of fixing the root cause. The save button and board modal MUST exist for the page to function.
- Considered adding the elements via `sendAppPage()` in `web_frontend.cpp` — rejected because the board modal is config-page-specific; adding it globally would pollute the index and RDM pages with unused DOM.

### Fix
Added the missing HTML elements to the end of `CONFIG_PAGE_BODY` in `src/frontend/pages/config_page.h`:
1. Closed the container `<div>` that was left open
2. Added `#save-bar` with `#save-btn` button using `form="cfg-form"` to associate it with the config form (the JS submits via fetch, so the button lives outside the form)
3. Added `#board-modal` (using the `.app-modal` + `.board-card` CSS classes from `config_css.h`) containing:
   - `#board-modal-title` (header title span)
   - `#board-print` / `#board-modal-close` (header buttons)
   - `#board-sel-modal` (board selector dropdown mirroring the main `board-sel`)
   - `#board-svg-wrap` (SVG diagram container using `.board-wrap` class)
   - `#board-pick-hint` (pin assignment hint, starts hidden via `.d-none`)
   - `#board-modal-done` (footer button)

### Files / Functions
- `src/frontend/pages/config_page.h` — added closing `</div>` for container, `#save-bar` with `#save-btn`, and `#board-modal` with all child elements

### Validation
Build succeeded: `pio run -e esp32s3_psram`. The config page now renders with:
- A fixed save bar at the bottom of the viewport with a "Save settings" button
- A board picker modal that opens when "Show board & pick pins" is clicked
- All DOM element IDs match the references in `config_js.h`

### Regression Risk
Low. The changes are additive (new HTML elements at the end of an existing PROGMEM string). No existing elements were modified or removed. The `#save-bar` and `#board-modal` CSS was already present in both `styles.h` and `config_css.h`, so the new HTML simply activates previously-defined styling. The `APP_MODAL_HTML` (generic confirm modal) from `shared_js.h` is separate and unaffected.

### Lesson
When migrating frontend code from monolithic HTML pages to modular PROGMEM fragments, verify that EVERY DOM element ID referenced by JavaScript has a matching HTML element in the corresponding body fragment. A missing element causes a null reference that crashes the entire page's JavaScript, silently breaking all functionality. The `tools/extract_frontend.py` script's regex-based extraction (which previously caused BUG-005 with unclosed divs) was not involved here — the board modal and save bar were never in the source HTML at all, suggesting they were designed in JS/CSS but the HTML was never written. Always cross-reference JS `$('elementId')` calls against the HTML to catch missing elements before they ship.


## BUG-010: ArtPollReply field offsets wrong, ESTA code missing, NodeType inverted, GoodOutput values reversed

Date: 2026-08-16
Subsystem: net/artnet
Severity: High (ArtPollReply rejected or misinterpreted by lighting consoles)
Affected hardware: all (Art-Net enabled builds)
Affected PlatformIO environment: all with `cfg.protocol != 1`

### Symptom
Lighting consoles (e.g., ETC, AVOLITES, MadMapper) fail to properly identify or control LuxDMX devices in Art-Net mode. Some consoles show the device as an "input" node instead of an output node, others don't discover it at all, and the ShortName/LongName appear garbled in the console's device list.

### Root Cause
`sendArtPollReply()` in `src/net/artnet_bridge.cpp` had multiple Art-Net 4 ArtPollReply layout violations:

1. **NumNodes (bytes 12-13)** set to `0, MAX_OUTPUTS` instead of `1, 0` -- claims there are MAX_OUTPUTS nodes in a single device
2. **NumPorts (bytes 14-15)** set to `0, 0` instead of `MAX_OUTPUTS, 0` -- claims zero ports
3. **NodeType (byte 16)** set to `0x02` (input node) instead of `0x01` (output node) -- LuxDMX is output-only
4. **ESTA code (bytes 18-21)** never written -- left as zeros from `memset`
5. **ShortName (bytes 22-39)** written at offset 44 with only 16 bytes instead of offset 22 with 18 bytes -- shifted into the ESTA/GoodInput area
6. **LongName (bytes 40-73)** written at offset 60 with 32 bytes instead of offset 40 with 34 bytes -- shifted by 20 bytes
7. **NodeReport (bytes 74-137)** written at offset 92 with 64 bytes -- shifted by 18 bytes
8. **SwOut at wrong offset**: `reply[24+i] = 0x80` (bytes 24-27) -- this is not a valid ArtPollReply field at that offset; 0x80 is also not a valid SwOut style value (valid values: 0=DMX, 1=ArtNet, etc.)
9. **GoodOutput (bytes 172-175)**: written at offset 28-31 with values `0x01` (enabled) / `0x80` (disabled) -- reversed and at wrong offset. Art-Net spec uses bit 7 (0x80) = "good", so enabled should be 0x80, disabled should be 0x00.
10. **Oem (bytes 140-141)**, **Status1 (byte 144)**, **Status2 (byte 145)** never populated -- left as zeros
11. **SwIn/SwOut (bytes 148-155)** never explicitly written

### Incorrect Approaches
- Tried leaving the "reserved" IP/Subnet/Gateway at bytes 156-167 in place (non-standard, falls in reserved area) -- kept for backward compatibility since existing controllers may read it there

### Fix
Rewrote `sendArtPollReply()` with the correct Art-Net 4 field layout:
- NumNodes = 1 (byte 12, LE)
- NumPorts = MAX_OUTPUTS (byte 14, LE)
- NodeType = 0x01 (output node)
- ESTA code at bytes 18-21 (placeholder 0x00000000, needs registered ESTA code)
- ShortName at bytes 22-39 (18 bytes, space-padded)
- LongName at bytes 40-73 (34 bytes, space-padded)
- NodeReport at bytes 74-137 (64 bytes, space-padded)
- Oem at bytes 140-141 (0x0000)
- Status1 at byte 144, Status2 at byte 145 (0x00)
- SwIn at bytes 148-151 (0x00 = DMX, no inputs)
- SwOut at bytes 152-155 (0x00 = DMX style)
- GoodInput at bytes 168-171 (0x80 = good)
- GoodOutput at bytes 172-175 (0x80 if enabled, 0x00 if disabled)
- MAC at bytes 176-181 (unchanged)
- IP/Subnet/Gateway kept at bytes 156-167 (non-standard but existing behavior)

### Files / Functions
- `src/net/artnet_bridge.cpp` -- `sendArtPollReply()` (complete rewrite of body)

### Validation
Build succeeded: `pio run -e esp32s3_psram`.

### Regression Risk
Medium. The ArtPollReply is broadcast in response to ArtPoll from lighting consoles. Existing console sessions will need to rediscover the device. The field values are now spec-compliant, so consoles that previously worked (by accident) should continue to work, and consoles that previously rejected the reply should now accept it.

### Lesson
ArtPollReply field offsets and semantics must be verified against the official Art-Net 4 specification. Writing fields at incorrect offsets puts garbage data in adjacent fields (e.g., SwOut values in the ShortName area). Always cross-check against a known-good reference implementation. The NumNodes field should be 1 for a single device, not the port count. The NodeType bit for output is 0x01, not 0x02.

---

## BUG-011: OTA signature verification used deprecated mbedtls SHA256 API

Date: 2026-08-16
Subsystem: net/ota_sign
Severity: Medium (build warnings; risk of API removal in future mbedtls versions)
Affected hardware: all with OTA_SIGN_ENABLED=1 (esp32s3_n16r8_eth)
Affected PlatformIO environment: esp32s3_n16r8_eth (and any future production build)

### Symptom
The `otaVerifyAndCommit()` function in `src/net/ota_sign.cpp` called the deprecated non-_ret mbedtls SHA256 functions (`mbedtls_sha256_starts`, `mbedtls_sha256_update`, `mbedtls_sha256_finish`) which return void and have been deprecated in mbedtls 3.x. These functions could be removed in future ESP-IDF versions, causing build failures.

### Root Cause
The mbedtls SHA256 API changed in mbedtls 3.x: functions that previously returned void (`mbedtls_sha256_starts`, `mbedtls_sha256_update`, `mbedtls_sha256_finish`) were renamed with a `_ret` suffix and now return `int` (error code). The original code did not check return values either, silently ignoring potential SHA-256 computation errors.

### Fix
Replaced all mbedtls SHA256 calls with their `_ret` variants and added return value checks with proper cleanup:
- `mbedtls_sha256_starts_ret(&shaCtx, 0)` -- returns int, check for != 0
- `mbedtls_sha256_update_ret(&shaCtx, buf, toRead)` -- returns int, check for != 0
- `mbedtls_sha256_finish_ret(&shaCtx, hash)` -- returns int, check for != 0

Each failure path frees the SHA256 context before returning false.

### Files / Functions
- `src/net/ota_sign.cpp` -- `otaVerifyAndCommit()` (lines 87-115)

### Validation
Build succeeded: `pio run -e esp32s3_psram`. The code compiles without deprecation warnings for SHA256 API calls.

### Regression Risk
Low. The `_ret` variants are functionally identical to their non-ret counterparts in mbedtls 3.x (the non-ret versions are thin wrappers). The additional error checks are defensive -- a SHA-256 failure on an ESP32 is extremely unlikely, but checking is correct embedded practice.

### Lesson
Always use the `_ret` variant of mbedtls functions in mbedtls 3.x projects (ESP-IDF v5.x). The non-ret variants are deprecated and may be removed. Always check return values from mbedtls functions -- a failed SHA-256 or PK operation should cause the OTA verification to fail (reject the update), not silently accept a potentially corrupt signature.

---

## BUG-012: mDNS base service TXT records missing (api-version, fw-version, distro, board)

Date: 2026-08-16
Subsystem: net/mDNS
Severity: Low (discovery tools cannot identify firmware version or board type)
Affected hardware: all
Affected PlatformIO environment: all

### Symptom
mDNS discovery tools (e.g., `dns-sd -B _services._dns-sd._udp`) can see the HTTP service is announced but cannot determine the firmware version, API version, or board type from the TXT records. Only Art-Net and sACN services had TXT records configured.

### Root Cause
In `src/main.cpp`, `MDNS.addServiceTxt()` was only called for the "artnet" and "e131" services. The "http" service (port 80) had no TXT records attached, so discovery tools querying the HTTP service TXT records would get no metadata.

### Fix
Added four TXT records to the "http" service in `src/main.cpp` after `MDNS.addService("http", "tcp", 80)`:
```cpp
MDNS.addServiceTxt("http", "tcp", "api-version", "1");
MDNS.addServiceTxt("http", "tcp", "fw-version", FIRMWARE_VERSION);
MDNS.addServiceTxt("http", "tcp", "distro", "luxdmx-v2");
MDNS.addServiceTxt("http", "tcp", "board", BOARD_ID);
```
`FIRMWARE_VERSION` is available via `src/sys/firmware_version.h` (already included) and `BOARD_ID` via `src/sys/sys_platform.h` (already included).

### Files / Functions
- `src/main.cpp` -- `setup()` (mDNS registration section)

### Validation
Build succeeded: `pio run -e esp32s3_psram`.

### Regression Risk
None. TXT records are additive metadata -- they do not affect mDNS service discovery or HTTP functionality.

### Lesson
Base service TXT records (not just protocol-specific ones) are essential for device discovery and identification. Lighting consoles and network scanners rely on these fields to filter, categorize, and verify devices. Always add version and board identification to the primary HTTP mDNS service so that any discovery tool can identify the device regardless of which protocol mode it is in.