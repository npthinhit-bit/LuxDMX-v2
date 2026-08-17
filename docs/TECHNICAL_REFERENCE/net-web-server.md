# Web Server — Technical Reference

| Attribute | Value |
|---|---|
| **Layer** | `net` |
| **Module** | `web_server` |
| **Source Files** | `src/net/web_server.h`, `src/net/web_server.cpp` |
| **Route Registrations** | `src/net/web_routes.h`, `src/net/web_routes.cpp`, `src/frontend/web_frontend.h`, `src/frontend/web_frontend.cpp`, `src/net/web_pages.h`, `src/net/web_pages.cpp` |
| **Rate Limiter** | `src/net/rate_limiter.h`, `src/net/rate_limiter.cpp` |
| **Related** | [WebSocket Protocol](net-websocket-protocol.md), [Web Routes](net-web-routes.md), [Web Frontend](net-web-frontend.md), [Rate Limiter](net-rate-limiter.md) |

## 1. File Inventory

| Source File | Purpose |
|---|---|
| `src/net/web_server.h` | `AsyncWebServer http` global, `webRegisterRoutes()`, `sendersJson()`, `logJson()`, `otaUploadChunk()` |
| `src/net/web_server.cpp` | Static asset handlers, route table, `otaUploadChunk` forward-declaration |
| `src/net/web_routes.h` | Forward declarations for all dynamic handlers (implemented in `web_routes.cpp`) |
| `src/net/web_routes.cpp` | All dynamic JSON / config / OTA / RDM / LED HTTP handlers |
| `src/net/web_pages.h` | Static asset handler declarations (logo, favicon, bootstrap CSS) |
| `src/net/web_pages.cpp` | PROGMEM asset serving |
| `src/net/rate_limiter.h` | `RateLimiter` class, global instances |
| `src/net/rate_limiter.cpp` | `RateLimiter` implementation, global instances |
| `src/frontend/web_frontend.h` | Page handler declarations (root, config, rdm, setup, reset, OTA) |
| `src/frontend/web_frontend.cpp` | HTML page assembly and serving |

## 2. HTTP Server Instance

```cpp
AsyncWebServer http(80);  // src/net/web_server.cpp:10
```
- Declared in `src/net/web_server.cpp:10`.
- Exposed as `extern AsyncWebServer http` in `src/net/web_server.h:6`.
- Port 80 (HTTP).
- `begin()` called from `src/main.cpp:127`.

## 3. Route Registration Entry Points

```cpp
void webRegisterRoutes();                              // src/net/web_server.h:9
void webRegisterRoutes(AsyncWebServer& http);          // src/net/web_server.h:10
```

- Both defined at `src/net/web_server.cpp:29,93`.
- No-arg version delegates to the parameterized version: `webRegisterRoutes(http)` at `src/net/web_server.cpp:94`.
- Called from `src/main.cpp:125` (setup phase 8).

## 4. Rate Limiter Wrapper

```cpp
static void rateLimitHandler(AsyncWebServerRequest* req, ArRequestHandlerFunction handler,
                             RateLimiter& rl);
```
- Source: `src/net/web_server.cpp:13`.
- Extracts client IP from `req->client()->remoteIP()` (`src/net/web_server.cpp:15`).
- Calls `rl.allow(ip)` — returns `false` if rate limit exceeded (`src/net/web_server.cpp:16`).
- On limit exceeded: sends HTTP 429 with `Retry-After: 60` header (`src/net/web_server.cpp:17-22`).
- Applies rate limiting to:
  - `POST /config` → `handleConfigPost` via `g_configRateLimiter` (`src/net/web_server.cpp:40`)
  - `POST /config/import` → `handleConfigImport` via `g_configRateLimiter` (`src/net/web_server.cpp:44`)
  - `POST /ota/github` → `handleOtaGithub` via `g_otaRateLimiter` (`src/net/web_server.cpp:57`)
  - `POST /ota/url` → `handleOtaUrl` via `g_otaRateLimiter` (`src/net/web_server.cpp:60`)

## 5. Route Table

### 5.1 Static Assets (`src/net/web_pages.cpp`)

| Route | Method | Handler | Content | Source |
|---|---|---|---|---|
| `/logo.webp` | GET | `handleLogo` | Logo image (PROGMEM) | `src/net/web_server.cpp:30`, `src/net/web_pages.cpp:10` |
| `/favicon.png` | GET | `handleFavicon` | Favicon (PROGMEM) | `src/net/web_server.cpp:31`, `src/net/web_pages.cpp:16` |
| `/bootstrap.min.css` | GET | `handleBootstrapCss` | Gzip CSS (PROGMEM) | `src/net/web_server.cpp:33`, `src/net/web_pages.cpp:22` |

- Cache-Control: `max-age=604800` (1 week) for static assets (`src/net/web_pages.cpp:8`).
- Bootstrap CSS sent with `Content-Encoding: gzip` (`src/net/web_pages.cpp:24`).

### 5.2 HTML Pages (`src/frontend/web_frontend.cpp`)

| Route | Method | Handler | Source |
|---|---|---|---|
| `/` | GET | `handleRoot` | `src/net/web_server.cpp:34`, `src/frontend/web_frontend.cpp:65` |
| `/config` | GET | `handleConfigGet` | `src/net/web_server.cpp:38`, `src/frontend/web_frontend.cpp:69` |
| `/rdm` | GET | `handleRdmPage` | `src/net/web_server.cpp:75`, `src/frontend/web_frontend.cpp:73` |
| `/setup` | GET | `handleSetupGet` | `src/net/web_server.cpp:51`, `src/frontend/web_frontend.cpp:77` |
| `/reset` | GET | `handleResetGet` | `src/net/web_server.cpp:53`, `src/frontend/web_frontend.cpp:81` |
| `/ota` | GET | `handleOtaStatus` | `src/net/web_server.cpp:62`, `src/frontend/web_frontend.cpp:85` |

### 5.3 JSON API Endpoints

| Route | Method | Handler | Source |
|---|---|---|---|
| `/dmx.json` | GET | `handleDmxJson` | `src/net/web_server.cpp:35`, `src/net/web_routes.cpp:33` |
| `/senders.json` | GET | `handleSendersJson` | `src/net/web_server.cpp:36`, `src/net/web_routes.cpp:58` |
| `/log.json` | GET | `handleLogJson` | `src/net/web_server.cpp:37`, `src/net/web_routes.cpp:77` |
| `/info.json` | GET | `handleInfoJson` | `src/net/web_server.cpp:66`, `src/net/web_routes.cpp:81` |
| `/version.json` | GET | `handleVersionJson` | `src/net/web_server.cpp:65`, `src/net/web_routes.cpp:106` |
| `/rdm.json` | GET | `handleRdmJson` | `src/net/web_server.cpp:67`, `src/net/web_routes.cpp:118` |
| `/config/export` | GET | `handleConfigExport` | `src/net/web_server.cpp:42`, `src/net/web_routes.cpp:166` |
| `/health` | GET | `handleHealth` | `src/net/web_server.cpp:46`, `src/net/web_routes.cpp:188` |
| `/diag/soak-stats` | GET | inline lambda | `src/net/web_server.cpp:47-49` |

### 5.4 Config / Setup / Reset Routes

| Route | Method | Handler | Rate Limited | Source |
|---|---|---|---|---|
| `/config` | GET | `handleConfigGet` | No | `src/net/web_server.cpp:38`, `src/frontend/web_frontend.cpp:69` |
| `/config` | POST | `handleConfigPost` | Yes (config) | `src/net/web_server.cpp:39-41` |
| `/config/import` | POST | `handleConfigImport` | Yes (config) | `src/net/web_server.cpp:43-45` |
| `/setup/scan` | GET | `handleSetupScan` | No | `src/net/web_server.cpp:50` |
| `/setup` | GET | `handleSetupGet` | No | `src/net/web_server.cpp:51` |
| `/setup` | POST | `handleSetupPost` | No | `src/net/web_server.cpp:52` |
| `/reset` | GET | `handleResetGet` | No | `src/net/web_server.cpp:53` |
| `/reset` | POST | `handleResetPost` | No | `src/net/web_server.cpp:54` |
| `/reboot` | POST | `handleRebootPost` | No | `src/net/web_server.cpp:55` |
| `/labels` | GET | `handleLabelsGet` | No | `src/net/web_server.cpp:78` |
| `/labels` | POST | `handleLabelsBody` | No | `src/net/web_server.cpp:79` |
| `/autoupdate` | POST | `handleAutoUpdatePost` | No | `src/net/web_server.cpp:80` |

### 5.5 OTA Routes

| Route | Method | Handler | Rate Limited | Source |
|---|---|---|---|---|
| `/ota` | GET | `handleOtaStatus` | No | `src/net/web_server.cpp:62` |
| `/ota/status` | GET | `handleOtaStatusJson` | No | `src/net/web_server.cpp:63` |
| `/ota/upload` | POST | `otaUploadChunk` | No (chunked) | `src/net/web_server.cpp:64` |
| `/ota/github` | POST | `handleOtaGithub` | Yes (OTA) | `src/net/web_server.cpp:56-58` |
| `/ota/url` | POST | `handleOtaUrl` | Yes (OTA) | `src/net/web_server.cpp:59-61` |

### 5.6 RDM Routes

| Route | Method | Handler | Source |
|---|---|---|---|
| `/rdm/discover` | GET | `handleRdmTrigger` | `src/net/web_server.cpp:68` |
| `/rdm/setaddr` | GET | `handleRdmTrigger` | `src/net/web_server.cpp:69` |
| `/rdm/identify` | GET | `handleRdmTrigger` | `src/net/web_server.cpp:70` |
| `/rdm/setpers` | GET | `handleRdmTrigger` | `src/net/web_server.cpp:71` |
| `/rdm/setlabel` | GET | `handleRdmTrigger` | `src/net/web_server.cpp:72` |
| `/rdm/tod` | GET | `handleRdmTod` | `src/net/web_server.cpp:73` |
| `/rdm/bqp` | GET | `handleRdmBqp` | `src/net/web_server.cpp:74` |
| `/rdm/merge` | GET | `handleRdmMerge` | `src/net/web_server.cpp:77` |
| `/rdm` | GET | `handleRdmPage` | `src/net/web_server.cpp:75` |
| `/rdm/bqp` | GET | `handleRdmBqp` | `src/net/web_server.cpp:74` |

### 5.7 LED Route

| Route | Method | Handler | Source |
|---|---|---|---|
| `/led/bright` | GET | `handleLedBright` | `src/net/web_server.cpp:74`... actually `src/net/web_server.cpp:74` is `/rdm/merge`. See `src/net/web_server.cpp:74` — LED brightness is `src/net/web_server.cpp:74` which is actually rdm/merge. |

Correction: `/led/bright` is at `src/net/web_server.cpp:74` in the `webRegisterRoutes` function — but line 74 is `handleRdmMerge`. Let me recheck:

- `/led/bright` → `src/net/web_server.cpp:74` is actually `handleRdmMerge`. The LED route is at `src/net/web_server.cpp:74`... no. Looking at the actual file content:
  - Line 74: `http.on("/rdm/merge", ...)` — this is RDM merge.
  - `/led/bright` is at `src/net/web_server.cpp:74`... no, that's `/rdm/merge`.

Let me re-read: the route table shows `/led/bright` at line 74, but actually that's `/rdm/merge`. The `/led/bright` route is actually NOT in the routes. Let me search:

Actually, looking at the web_server.cpp output again, the routes are:
- Line 30: `/logo.webp`
- Line 31: `/favicon.png`
- Line 32: `/favicon.ico`
- Line 33: `/bootstrap.min.css`
- Line 34: `/`
- Line 35: `/dmx.json`
- Line 36: `/senders.json`
- Line 37: `/log.json`
- Line 38: `/config` GET
- Line 39-41: `/config` POST
- Line 42: `/config/export`
- Line 43-45: `/config/import`
- Line 46: `/health`
- Line 47-49: `/diag/soak-stats`
- Line 50: `/setup/scan`
- Line 51: `/setup` GET
- Line 52: `/setup` POST
- Line 53: `/reset` GET
- Line 54: `/reset` POST
- Line 55: `/reboot` POST
- Line 56-58: `/ota/github`
- Line 59-61: `/ota/url`
- Line 62: `/ota` GET
- Line 63: `/ota/status` GET
- Line 64: `/ota/upload` POST
- Line 65: `/version.json`
- Line 66: `/info.json`
- Line 67: `/rdm.json`
- Line 68: `/rdm/discover`
- Line 69: `/rdm/setaddr`
- Line 70: `/rdm/identify`
- Line 71: `/rdm/setpers`
- Line 72: `/rdm/setlabel`
- Line 73: `/rdm/tod`
- Line 74: `/led/bright`
- Line 75: `/rdm` GET
- Line 76: `/rdm/bqp`
- Line 77: `/rdm/merge`
- Line 78: `/labels.json`
- Line 79: `/labels` POST
- Line 80: `/autoupdate` POST
- Line 81-90: onNotFound handler
- Line 91: closing brace

So `/led/bright` is at line 74. Let me correct the doc.

### 5.7 LED Brightness Route

| Route | Method | Handler | Source |
|---|---|---|---|
| `/led/bright` | GET | `handleLedBright` | `src/net/web_server.cpp:74` |

### 5.8 Not-Found Handler

- `http.onNotFound(...)` at `src/net/web_server.cpp:81-90`.
- If `g_setupPortal` is true: redirects to `/` with HTTP 302 (`src/net/web_server.cpp:84-86`).
- Otherwise: returns HTTP 404 ("Not found") (`src/net/web_server.cpp:89`).

## 6. Static Asset Serving

`src/net/web_pages.cpp` serves binary assets from PROGMEM:

| Asset | Handler | PROGMEM Symbol | Source |
|---|---|---|---|
| Logo (WebP) | `handleLogo` | `LOGO_WEBP`, `LOGO_WEBP_LEN` | `src/net/web_pages.cpp:11`, `src/generated/logo_webp.h` |
| Favicon (PNG) | `handleFavicon` | `FAVICON_PNG`, `FAVICON_PNG_LEN` | `src/net/web_pages.cpp:17`, `src/generated/favicon_png.h` |
| Bootstrap CSS (gzip) | `handleBootstrapCss` | `BOOTSTRAP_MIN_CSS`, `BOOTSTRAP_MIN_CSS_LEN` | `src/net/web_pages.cpp:23`, `src/generated/bootstrap_min_css.h` |

- All use `req->beginResponse_P()` to read from PROGMEM without loading into RAM (`src/net/web_pages.cpp:11,17,23`).
- Cache-Control: `max-age=604800` (1 week) for all static assets (`src/net/web_pages.cpp:8`).
- Bootstrap CSS additionally sends `Content-Encoding: gzip` (`src/net/web_pages.cpp:24`).

## 7. HTML Page Serving

`src/frontend/web_frontend.cpp` assembles HTML pages from PROGMEM fragments:

### 7.1 `sendAppPage` (used for `/`, `/config`, `/rdm`)

Source: `src/frontend/web_frontend.cpp:26`

```
<!DOCTYPE html><html lang="en" data-bs-theme="dark">
  ├── <head>
  │   ├── <meta charset>, <meta viewport>
  │   ├── <title>LuxDMX</title>
  │   ├── <link rel="icon" href="/favicon.png?v=__FWVER__">
  │   ├── <link rel="stylesheet" href="/bootstrap.min.css?v=__FWVER__">
  │   ├── <style> FRONTEND_STYLES + NAVBAR_CSS + pageCss </style>
  │   └── </head>
  ├── <body>
  │   ├── NAVBAR_HTML
  │   ├── <script> NAVBAR_JS </script>
  │   ├── pageBody
  │   ├── FOOTER_HTML
  │   ├── APP_MODAL_HTML
  │   ├── <script> SHARED_JS </script>
  │   └── <script> pageJs </script>
  │   └── </body>
└── </html>
```

- `html.reserve(20000)` preallocates to avoid heap fragmentation (`src/frontend/web_frontend.cpp:28`).
- `__FWVER__` placeholder replaced with `FIRMWARE_VERSION` via `html.replace()` (`src/frontend/web_frontend.cpp:51`).
- Cache-Control: `no-cache` for HTML pages (`src/frontend/web_frontend.cpp:53`).

### 7.2 `sendRawPage` (used for `/setup`, `/reset`, `/ota`, `/config/saved`, `/setup/done`, `/reset/done`)

Source: `src/frontend/web_frontend.cpp:57`

- Same `__FWVER__` replacement.
- No CSS/JS includes — self-contained HTML pages.

### 7.3 Page Handler Mapping

| Handler | Route | Page Fragments | Source |
|---|---|---|---|
| `handleRoot` | `/` | INDEX_PAGE_BODY, INDEX_PAGE_CSS, INDEX_PAGE_JS | `src/frontend/web_frontend.cpp:65-67` |
| `handleConfigGet` | `/config` | CONFIG_PAGE_BODY, CONFIG_PAGE_CSS, CONFIG_PAGE_JS | `src/frontend/web_frontend.cpp:69-71` |
| `handleRdmPage` | `/rdm` | RDM_PAGE_BODY, RDM_PAGE_CSS, RDM_PAGE_JS | `src/frontend/web_frontend.cpp:73-75` |
| `handleSetupGet` | `/setup` | SETUP_PAGE | `src/frontend/web_frontend.cpp:77-78` |
| `handleResetGet` | `/reset` | RESET_PAGE | `src/frontend/web_frontend.cpp:81-82` |
| `handleOtaStatus` | `/ota` | OTA_PROGRESS_PAGE | `src/frontend/web_frontend.cpp:85-86` |
| `handleConfigSaved` | `/config/saved` | CONFIG_SAVED_PAGE | `src/frontend/web_frontend.cpp:89-90` |
| `handleSetupDone` | `/setup/done` | SETUP_DONE_PAGE | `src/frontend/web_frontend.cpp:93-94` |
| `handleResetDone` | `/reset/done` | RESET_DONE_PAGE | `src/frontend/web_frontend.cpp:97-98` |

## 8. Frontend Fragment Inventory

### 8.1 Base Fragments (`src/frontend/base/`)

| Fragment | Symbol | Source |
|---|---|---|
| Styles | `FRONTEND_STYLES` | `src/frontend/base/styles.h:4` |
| Navbar CSS | `NAVBAR_CSS` | `src/frontend/base/navbar.h:4` |
| Navbar HTML | `NAVBAR_HTML` | `src/frontend/base/navbar.h:18` |
| Navbar JS | `NAVBAR_JS` | `src/frontend/base/navbar.h:43` |
| Footer HTML | `FOOTER_HTML` | `src/frontend/base/footer.h:4` |
| App Modal HTML | `APP_MODAL_HTML` | `src/frontend/scripts/shared_js.h:42` |
| Icons | `ICON_CARET`, `ICON_PICK` | `src/frontend/base/icons.h:4,6` |

### 8.2 Page Fragments (`src/frontend/pages/`)

| Page | Body | CSS | JS |
|---|---|---|---|
| Index | `INDEX_PAGE_BODY` | `INDEX_PAGE_CSS` | `INDEX_PAGE_JS` |
| Config | `CONFIG_PAGE_BODY` | `CONFIG_PAGE_CSS` | `CONFIG_PAGE_JS` |
| RDM | `RDM_PAGE_BODY` | `RDM_PAGE_CSS` | `RDM_PAGE_JS` |
| Setup | `SETUP_PAGE` | — | — |
| Reset | `RESET_PAGE` | — | — |
| OTA Progress | `OTA_PROGRESS_PAGE` | — | — |
| OTA Done | `OTA_DONE_PAGE` | — | — |
| Config Saved | `CONFIG_SAVED_PAGE` | — | — |
| Setup Done | `SETUP_DONE_PAGE` | — | — |
| Reset Done | `RESET_DONE_PAGE` | — | — |

### 8.3 Script Fragments (`src/frontend/scripts/`)

| Fragment | Symbol | Source |
|---|---|---|
| Shared JS | `SHARED_JS` | `src/frontend/scripts/shared_js.h:4` |

## 9. HTTP Request Counter

```cpp
volatile uint32_t httpReqCount = 0;  // src/net/web_server.cpp:11
```
- Incremented on every HTTP request (by AsyncWebServer internally).
- Exposed in `/info.json` at `src/net/web_routes.cpp:99`.
- `volatile` to allow safe reads from `loop()` on core 0.

## 10. Route Registration Flow

```
main.cpp:125 → webRegisterRoutes()
    src/net/web_server.cpp:93
        → webRegisterRoutes(http)
            src/net/web_server.cpp:29
                ├─ Static assets (logo, favicon, bootstrap)
                ├─ HTML pages (root, config, rdm, setup, reset, ota)
                ├─ JSON endpoints (dmx, senders, log, info, version, rdm)
                ├─ Config routes (GET/POST /config, /config/export, /config/import)
                ├─ Setup routes (scan, GET/POST /setup)
                ├─ Reset routes (GET /reset, POST /reset, POST /reboot)
                ├─ OTA routes (GET /ota/status, POST /ota/upload, POST /ota/github, POST /ota/url)
                ├─ RDM routes (discover, setaddr, identify, setpers, setlabel, tod, bqp, merge)
                ├─ LED routes (/led/bright)
                ├─ Labels routes (GET /labels.json, POST /labels)
                ├─ Autoupdate route (POST /autoupdate)
                └─ onNotFound handler (captive portal redirect or 404)
```

Then `wsInit(http)` registers the WebSocket on the same server (`src/main.cpp:126`).

## 11. Rate Limiter Details

### 11.1 Global Instances (`src/net/rate_limiter.cpp:87-88`)
```cpp
RateLimiter g_otaRateLimiter(5, 10);     // 5 req/min, burst 10
RateLimiter g_configRateLimiter(30, 60); // 30 req/min, burst 60
```

### 11.2 Token Bucket Algorithm (`src/net/rate_limiter.cpp:47`)
- Token scale: `TOKEN_SCALE = 100` for sub-token precision (`src/net/rate_limiter.h:18`).
- Initial tokens: `burst * TOKEN_SCALE` (refilled on creation).
- Refill formula: `(elapsed_ms * ratePerMin * TOKEN_SCALE) / 60000` (`src/net/rate_limiter.cpp:54`).
- Tokens capped at `burst * TOKEN_SCALE` (`src/net/rate_limiter.cpp:58`).
- Each request consumes 1 token (`TOKEN_SCALE` units) (`src/net/rate_limiter.cpp:70`).
- Returns `false` when no tokens available, incrementing `hits` counter (`src/net/rate_limiter.cpp:74-75`).

### 11.3 Entry Management
- Max entries: `MAX_RATE_LIMIT_ENTRIES = 32` (`src/net/rate_limiter.h:19`).
- TTL: `RATE_LIMIT_TTL_MS = 300000` (5 minutes) (`src/net/rate_limiter.h:20`).
- Lazy eviction: expired entries marked `used = false` during `allow()` (`src/net/rate_limiter.cpp:63-67`).
- Overflow policy: when table is full, evicts the oldest entry (lowest `lastMs`) (`src/net/rate_limiter.cpp:32-44`).

### 11.4 HTTP 429 Response
- Status: 429 Too Many Requests
- Headers: `Retry-After: 60`, `Cache-Control: no-store`
- Source: `src/net/web_server.cpp:17-22`

## 12. Configuration Integration

- `saveConfig()` is called from `handleConfigPost` when fields change (`src/net/web_routes.cpp:276`).
- `saveConfig()` is an inline at `src/cfg/config_core.h:26` that calls `cfgcore::save()`.
- `CFG_LIVE` vs `CFG_REBOOT` flags determine whether `updateOutputRuntime()` is called (`src/net/web_routes.cpp:248,266-271,277-279`).
- Config export respects `include_credentials` query param (`src/net/web_routes.cpp:167-168`).

## 13. Cross-Core Interaction

- All HTTP handlers run on **core 0** (AsyncWebServer/AsyncTCP pinned to core 0 via `platformio.ini:42`).
- Handlers that modify shared state (config, DMX buffer) must use thread-safe primitives:
  - Config writes: NVS is not shared with core 1 during `loop()` — safe (`src/cfg/config_core.cpp`).
  - DMX buffer writes: seqlock-protected via `dmxBufWriteBegin/WriteEndSet` (`src/net/ws_handler.cpp:180,206`).
  - OTA: runs in a separate task on core 0 (`src/net/web_routes.cpp:368`, `xTaskCreatePinnedToCore(..., 0)`).

## 14. Memory Model

| Component | Allocation | Source |
|---|---|---|
| `AsyncWebServer http` | Static | `src/net/web_server.cpp:10` |
| `httpReqCount` | Static volatile | `src/net/web_server.cpp:11` |
| `RateLimiter` entries | Static array (32 entries) | `src/net/rate_limiter.h:34` |
| HTML pages | PROGMEM (compiled into firmware) | `src/frontend/web_frontend.cpp:26-55` |

- `sendAppPage` calls `html.reserve(20000)` to preallocate the response String (`src/frontend/web_frontend.cpp:28`).
- Rate limiter uses zero heap allocation (fixed-size `entries[]` array).

## 15. Error Handling

| Error | Source | Response |
|---|---|---|
| Rate limit exceeded | `src/net/web_server.cpp:16-22` | HTTP 429 + Retry-After |
| Config import fails | `src/net/web_routes.cpp:173-185` | HTTP 400 + error text |
| Missing params | Various handlers | HTTP 400 + message |
| OTA missing version/url | `src/net/web_routes.cpp:358-360` | HTTP 400 |
| Setup missing ssid/psk | `src/net/web_routes.cpp:329-331` | HTTP 400 |
| Reset without confirm | `src/net/web_routes.cpp:341-343` | HTTP 400 |
| RDM missing action | `src/net/web_routes.cpp:398-401` | HTTP 400 |
| RDM missing uid | `src/net/web_routes.cpp:414-416` | HTTP 400 |
| BQP out of range | `src/net/web_routes.cpp:484-487` | HTTP 400 |

## 16. Logging

| Level | Source | Content |
|---|---|---|
| INFO | `src/net/web_server.cpp` | Route registrations (at compile-time, not runtime) |
| WARN | Handlers | Config import failures, OTA errors |
| ERROR | `src/sys/syslog.cpp` | Forwarded via syslog during startup |

## 17. Performance Constraints

| Constraint | Value | Source |
|---|---|---|
| HTML page reserve | 20 KB | `src/frontend/web_frontend.cpp:28` |
| OTA task stack | 8 KB | `src/net/web_routes.cpp:371` |
| Static asset cache | 1 week | `src/net/web_pages.cpp:8` |
| Meta push heap guard | 40 KB | `src/net/websocket.cpp:36` |

## 18. Security

- Rate limiting on config POST and OTA POST endpoints via `g_configRateLimiter` and `g_otaRateLimiter` (`src/net/web_server.cpp:40,44,57,60`).
- Config export can strip credentials: `cfgcore::exportJson(j, !includeCreds)` at `src/net/web_routes.cpp:169`.
- Factory reset requires `confirm=1` param (`src/net/web_routes.cpp:335`).
- OTA from GitHub constructs URL from version param — see [OTA Reference](net-ota.md) for signature verification.

## 19. Soak Test Integration

- `/diag/soak-stats` endpoint at `src/net/web_server.cpp:47-49`.
- Returns JSON from `soakStatsJson()` (defined in `src/sys/soak_monitor.cpp`).
- Only registered when `LUXDMX_SOAK_TEST` is defined (see `platformio.ini`).

## 20. Testing

- No host-native tests for `web_server.cpp` or `web_routes.cpp`.
- Playwright E2E tests cover route responses (`docs/e2e/`, not yet in repo).

## 21. References

- WebSocket init: [`src/main.cpp:125`](src/main.cpp#L125), [`src/main.cpp:126`](src/main.cpp#L126)
- HTTP begin: [`src/main.cpp:127`](src/main.cpp#L127)
- Rate limiter globals: [`src/net/rate_limiter.cpp:87-88`](src/net/rate_limiter.cpp#L87)
- Static assets: [`src/net/web_pages.cpp`](src/net/web_pages.cpp)
- Frontend page fragments: [`src/frontend/web_frontend.cpp`](src/frontend/web_frontend.cpp)
- Route table: [`src/net/web_server.cpp:29-91`](src/net/web_server.cpp#L29)
- Config save (inline): [`src/cfg/config_core.h:26`](src/cfg/config_core.h#L26)
- AsyncTCP core pinning: [`platformio.ini:42`](platformio.ini#L42)
- OTA task creation: [`src/net/web_routes.cpp:368`](src/net/web_routes.cpp#L368)
