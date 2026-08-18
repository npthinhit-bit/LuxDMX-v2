# Plan: Fix Blank Web Page - Missing Gzip Content-Encoding Header

## Problem
User accesses `http://192.168.137.249/` (device IP from serial log) but receives a blank page - no HTML content rendered.

## Root Cause Analysis
- HTML pages (`index.html`, `config.html`, `rdm.html`) are **pre-gzipped at build time** by `extra_scripts.py` (lines 98-102)
- They are served via `sendAppPage()` in `src/frontend/web_frontend.cpp`
- **Critical bug**: `sendAppPage()` sends response as `"text/html"` **without** `Content-Encoding: gzip` header
- Browser receives compressed binary data but treats it as plain text → blank page
- Contrast: `web_pages.cpp:27` correctly adds `Content-Encoding: gzip` for `bootstrap.min.css`

## Evidence from Codebase
| File | Line | Issue |
|------|------|-------|
| `extra_scripts.py` | 98-102 | `index.html`, `config.html`, `rdm.html` gzipped at build |
| `web_frontend.cpp` | 64-73 | `sendAppPage()` missing gzip header |
| `web_pages.cpp` | 27 | Correct pattern: `r->addHeader("Content-Encoding", "gzip")` |

## Fix Required
**File**: `src/frontend/web_frontend.cpp`
**Function**: `sendAppPage()` (lines 28-74)
**Change**: Add gzip header before `req->send(r)`

```cpp
// Line ~64-73, after creating response:
AsyncWebServerResponse* r = req->beginResponse("text/html", htmlLen, [sp](...){...});
r->addHeader("Content-Encoding", "gzip");  // ← ADD THIS LINE
r->addHeader("Cache-Control", "no-cache");
req->send(r);
```

Same fix needed for `sendRawPage()` (lines 76-93) which serves `setup.html`, `reset.html`, `ota_progress.html`, etc. - these are **NOT gzipped** (raw HTML in `extra_scripts.py:103-111`), so they should NOT get the gzip header.

## Affected Pages
| Page | Gzipped? | Handler | Fix Needed |
|------|----------|---------|------------|
| `/` (index.html) | Yes | `handleRoot` → `sendAppPage` | **YES** - add gzip header |
| `/config` | Yes | `handleConfigGet` → `sendAppPage` | **YES** - add gzip header |
| `/rdm` | Yes | `handleRdmPage` → `sendAppPage` | **YES** - add gzip header |
| `/setup` | No | `handleSetupGet` → `sendRawPage` | NO - raw HTML |
| `/reset` | No | `handleResetGet` → `sendRawPage` | NO - raw HTML |
| `/ota` | No | `handleOtaStatus` → `sendRawPage` | NO - raw HTML |
| `/config/saved` | No | `handleConfigSaved` → `sendRawPage` | NO - raw HTML |

## Validation Steps
1. Build: `pio run -e esp32s3_psram`
2. Flash: `pio run -e esp32s3_psram --target upload --upload-port COM7`
3. Monitor: `pio run -e esp32s3_psram --target monitor --upload-port COM7` - verify IP `192.168.137.249`
4. Test with curl:
   ```bash
   curl -v http://192.168.137.249/  # Should return HTML with Content-Encoding: gzip
   curl -v http://192.168.137.249/config  # Should return HTML with Content-Encoding: gzip
   curl -v http://192.168.137.249/setup  # Should return HTML WITHOUT gzip header
   ```
5. Browser test: Open `http://192.168.137.249/` - should render LuxDMX dashboard

## Risk Assessment
- **Low risk**: One-line header addition, matches existing pattern in `web_pages.cpp`
- **No regression**: Only affects 3 gzipped pages; raw pages unchanged
- **Build gate**: Must pass `pio run -e esp32s3_psram` (project constraint)

## Dependencies
- None - isolated change to `web_frontend.cpp`
- Build-time gzip generation already works (verified by `src/generated/` files existing)

## Rollout
- Single commit, no migration needed
- Test on `esp32s3_psram` env first (user's current hardware)
- Then verify other envs build: `esp32dev`, `esp32s3dev`, `wt32eth01`, `esp32s3_n16r8_eth`