# Web Server — System Specification

Domain: net.web-server

## 1. Module Overview

The Web Server subsystem hosts the HTTP listener and the static-asset/SPA serving layer for the LuxDMX gateway. It instantiates a single AsyncWebServer on port 80 and registers the full route table (static assets, HTML pages, JSON API endpoints, config, setup, OTA, RDM, and diagnostics) during system bring-up. It also exposes the route-registration entry point used by every other web-facing module, and provides the not-found fallback that powers the setup-portal captive redirect.

The server runs entirely on core 0 at task priority 5, sharing that core with the WiFi/lwIP stack, the Art-Net/sACN receive task, the serial console, and all network protocol parsing. AsyncTCP is pinned to core 0 with a hardened 16 KB stack and a queue depth of 128, isolating the time-critical core-1 DMX transmit path from all HTTP processing.

**Owns:** HTTP server instance (port 80), route registration wiring, not-found fallback, HTTP request counter.
**Delegates to:** Web Routes (dynamic JSON / config / OTA / RDM handlers), Web Frontend (HTML page assembly), Web Pages (PROGMEM static assets), Rate Limiter (rate-gated handlers).
**Consumed by:** Browser clients, external HTTP tools, the WebSocket subsystem (shares the same server instance).

## 2. External Interfaces

### 2.1 HTTP Server Instance

| Interface | Caller | Purpose |
|---|---|---|
| AsyncWebServer (port 80) | System bring-up | Single HTTP listener; begin() invoked during setup phase 8 |
| webRegisterRoutes() | main.cpp setup phase 8 | Register all HTTP routes onto the server instance |
| webRegisterRoutes(http) | webRegisterRoutes() | Parameterized registration binding routes to a given server |
| httpReqCount (volatile counter) | main.cpp loop / WebSocket push | Tracks total HTTP requests for instrumentation |

### 2.2 Route Categories Registered

The server registers routes across these categories:

- **Static binary assets** — logo (WebP), favicon (PNG), Bootstrap CSS (gzip-compressed).
- **HTML pages** — index (status), config, RDM fixtures, setup portal, reset confirmation, OTA progress/flow.
- **JSON API endpoints** — DMX channels, senders, change log, device info, firmware version, RDM status.
- **Config routes** — GET schema form, POST config save, config export, config import.
- **Setup / network provisioning** — WiFi scan, POST setup, factory reset, reboot.
- **OTA routes** — GitHub release, URL fetch, local chunked upload, OTA status.
- **RDM action routes** — device discovery, address set, identify, personality, label, TOD, BQP, merge mode.
- **Diagnostics** — health check, soak-monitor stats (conditional).
- **Not-found handler** — captive-portal redirect or 404.

## 3. State Machine

No internal state machine. The server instance is a passive event dispatcher: it receives HTTP requests, matches them against the registered route table, and invokes the corresponding handler. The only mutable state owned directly by the server layer is the HTTP request counter (a monotonically increasing volatile integer).

The captive-portal setup flag (a boolean) governs the not-found fallback behavior: when set, unmatched routes redirect to `/` (HTTP 302); otherwise they return 404.

## 4. Data Flow

### 4.1 HTTP Request Lifecycle

1. An incoming TCP connection arrives on port 80; the AsyncWebServer accepts it on core 0.
2. The request line and headers are parsed; the path, method, and query parameters are extracted.
3. The route table is matched in registration order; the first matching handler is invoked.
4. If no route matches, the not-found handler runs: it checks the captive-portal flag — if true, it issues an HTTP 302 redirect to `/`, otherwise it returns a 404 response.
5. The handler produces a response: either a static PROGMEM asset (beginResponse_P), an assembled HTML page (String-based), or a JSON payload.
6. For rate-gated handlers, the rate-limiter wrapper is invoked first; it extracts the client IP, consults the token bucket, and either calls the underlying handler or short-circuits with an HTTP 429 response.

### 4.2 Static Asset Delivery

Static assets (logo, favicon, Bootstrap CSS) are stored as PROGMEM byte arrays and served via beginResponse_P, which reads directly from program memory without loading the asset into DRAM heap. Bootstrap CSS is additionally flagged with Content-Encoding: gzip so the browser decompresses it in place. All static assets carry a one-week cache-control header.

## 5. Configuration Integration

| Config Field | Apply Semantics | Usage in Web Server |
|---|---|---|
| LUXDMX_SOAK_TEST (build flag) | Compile-time | Gates registration of /diag/soak-stats route |
| g_setupPortal (runtime flag) | Live | Controls not-found fallback (redirect vs 404) |

The web server does not read schema-driven configuration fields directly. Configuration field consumption lives in the Web Routes module (config import/export, setup, OTA, RDM triggers). The server layer only consumes the compile-time soak-test flag and the runtime setup-portal boolean.

## 6. Lifecycle

1. **Init (setup phase 8):** webRegisterRoutes() is called from main.cpp, which invokes webRegisterRoutes(http) — the parameterized form that binds all routes to the global AsyncWebServer instance.
2. **Serve:** http.begin() is called immediately after route registration (setup phase 8). The server then runs as an event-driven loop on core 0, processing requests as they arrive.
3. **Tear-down:** No explicit deinit. The server is a static global instance; it is destroyed implicitly at power-off.

No background task is spawned by the server itself — it is driven entirely by the AsyncTCP event loop on core 0.

## 7. Error Handling

| Condition | Behavior |
|---|---|
| Rate limit exceeded | HTTP 429 with Retry-After: 60 and Cache-Control: no-store headers |
| Config import failure | HTTP 400 with error text message |
| Missing required parameter | HTTP 400 with descriptive message |
| OTA missing version or URL | HTTP 400 |
| Setup missing SSID or PSK | HTTP 400 |
| Factory reset without confirm | HTTP 400 |
| RDM action missing action parameter | HTTP 400 |
| RDM action missing UID | HTTP 400 |
| BQP value out of range | HTTP 400 |
| Route not found, setup portal active | HTTP 302 redirect to / |
| Route not found, setup portal inactive | HTTP 404 "Not found" |
| Rate-limiter entry-table full | Evicts the oldest entry (least-recently-used by timestamp) |

## 8. Timing Constraints

- All HTTP request processing occurs on core 0 within the AsyncTCP event-task context at priority 5.
- The AsyncTCP task is configured with a 16 KB stack and a queue depth of 128 (set via platformio.ini build flags).
- Rate-limit token-bucket refill is computed from elapsed wall-clock milliseconds; no blocking delays are used.
- Rate-limit entries expire after a 5-minute TTL; expired entries are lazily evicted during allow() checks.
- The HTTP request counter is incremented per request and is safe to read from the core-0 main loop (volatile, single-core access).

## 9. Memory and Allocation Model

All server-owned storage is static or PROGMEM:

- The AsyncWebServer instance is a file-scoped static global.
- The HTTP request counter is a file-scoped volatile uint32.
- Static binary assets (logo, favicon, Bootstrap CSS) reside in PROGMEM (generated header arrays).
- Route handlers do not allocate on the server side beyond what the underlying AsyncWebServer and String class require per request.
- The rate-limiter entry table is a fixed-size static array (32 entries); zero heap allocation.

## 10. Safety Considerations

- **Core isolation:** The web server and all HTTP handlers execute on core 0, never preempting the core-1 DMX transmit task. This guarantees that a slow HTTP request or a flood of client connections cannot interfere with DMX break/mark bit timing.
- **Non-blocking rate limiting:** Rate-limited handlers return immediately with an HTTP 429 when the token bucket is empty; they never block or stall the event loop.
- **Static assets from PROGMEM:** Serving logo/favicon/CSS from program memory avoids consuming limited DRAM heap, reducing the risk of heap fragmentation under concurrent requests.
- **No DMX buffer writes on core 0:** The web server itself performs no direct writes to the DMX buffer. Any handler that must modify channel data (e.g., via the WebSocket path) does so through the seqlock write protocol, ensuring tear-free access by the core-1 transmit task.
- **Setup-portal redirect:** Unmatched routes redirect to `/` only when the setup-portal flag is active, preventing accidental exposure of a generic 404 page during network provisioning.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| net.web-routes | owned by web server | Implements all dynamic JSON / config / OTA / RDM / setup handlers registered onto the server |
| net.web-frontend | owned by web server | Assembles HTML pages from PROGMEM fragments; handles app-page routes (/, /config, /rdm) |
| net.web-pages | owned by web server | Serves binary static assets (logo, favicon, Bootstrap CSS) from PROGMEM |
| net.rate-limiter | wraps handlers | Token-bucket rate limiting applied to POST /config, POST /ota/github, POST /ota/url |
| net.websocket | shares server | Shares the same AsyncWebServer instance for the /ws endpoint; wsInit(http) called after routes |
| sys.tasks | schedules core 0 | AsyncTCP event task runs on core 0 at the configured stack size and queue depth |
| platformio.ini | build flags | CONFIG_ASYNC_TCP_STACK_SIZE, CONFIG_ASYNC_TCP_QUEUE_SIZE, CONFIG_ASYNC_TCP_RUNNING_CORE, CONFIG_ASYNC_TCP_PRIORITY |

## 12. Testing Verification

- No host-native unit tests cover the web server route table, static-asset serving, or the not-found fallback.
- The route table is exercised end-to-end through browser-based E2E tests (Playwright) that drive HTTP requests against a live device and verify response codes, headers, and body content for each registered route.
- Rate-limit behavior (HTTP 429 responses) is covered by the same E2E suite, which issues burst requests and verifies throttling.
- The static-asset cache headers and gzip content-encoding are verified through the E2E asset-loading tests.
- The not-found captive-redirect path is validated in the setup-portal E2E tests.

**Untested paths:**
- Direct programmatic invocation of webRegisterRoutes() in isolation.
- Static asset PROGMEM boundary correctness under memory pressure.
- Rate-limiter full-table eviction policy in isolation.

## 13. Open Questions

1. Whether the HTTP 429 Retry-After value (fixed at 60 seconds) should be dynamic based on remaining token refill time.
2. Whether the not-found handler should differentiate between a true 404 and an intentional captive-bypass, to better support clients that do not honor redirect-to-self.
3. Whether the one-week static-asset cache TTL should be version-busted via query parameter on all asset types, or only on HTML-referenced assets.
4. Whether the request counter should be reset or snapshotted periodically, or is solely for instrumentation.

## 14. History

No recorded changes.
