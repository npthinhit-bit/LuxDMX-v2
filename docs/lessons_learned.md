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
