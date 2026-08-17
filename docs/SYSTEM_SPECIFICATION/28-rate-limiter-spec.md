# Rate Limiter — System Specification

Domain: net.rate-limiter

## 1. Module Overview

The Rate Limiter is an IP-based token-bucket rate-limiting subsystem that protects
state-modifying HTTP endpoints from abuse, brute-force attempts, and excessive flash
wear. It is applied to the firmware update endpoints (`POST /ota/github`,
`POST /ota/url`) and the configuration persistence endpoints (`POST /config`,
`POST /config/import`).

Each client IP address is tracked in a fixed-size entry table. Tokens accrue over time
based on a configured per-minute refill rate up to a burst ceiling. When the bucket is
empty, the incoming request is denied and an HTTP 429 response is returned to the client.

The limiter supports a configurable **severity** policy that controls how over-limit
requests are handled:

- **disabled** — Rate limiting is not applied; all requests proceed unconditionally.
- **advisory** — The request is allowed to proceed, but an over-limit warning metric
  is recorded (the client is observed but not blocked).
- **warning** — The request is allowed to proceed, but the event is logged for
  operator visibility.
- **error** — The request is dropped; an HTTP 429 Too Many Requests response is
  returned with `Retry-After` and `Cache-Control: no-store` headers. (This is the
  enforced mode for both endpoint groups.)

**Owns:** the per-client token-bucket entry table, token refill computation, and
the deny decision logic.
**Delegates to:** none — the limiter is self-contained.
**Consumed by:** Web Server (applies the limiter as a wrapper around rate-gated
HTTP handlers).

## 2. External Interfaces

### 2.1 Entry Points

| Interface | Direction | Purpose |
|---|---|---|
| `rateLimitHandler(request, handler, limiter)` | Web Server wrapper | Wraps a rate-gated handler. Extracts the client IP, consults the limiter, and either invokes the underlying handler or short-circuits with HTTP 429. |
| `allow(ip)` | Web Server ? Limiter | Evaluates whether a request from the given client IP is permitted under the current token-bucket state. Returns `true` (allowed) or `false` (denied). |
| `getHits(ip)` | Diagnostics | Returns the consecutive over-limit hit count for a given client IP. Returns 0 if the IP is not being tracked. |

### 2.2 Endpoint Assignments

| Endpoint | Limiter Instance | Rate | Burst |
|---|---|---|---|
| `POST /ota/github` | OTA Rate Limiter | 5 per minute | 10 |
| `POST /ota/url` | OTA Rate Limiter | 5 per minute | 10 |
| `POST /config` | Config Rate Limiter | 30 per minute | 60 |
| `POST /config/import` | Config Rate Limiter | 30 per minute | 60 |

### 2.3 Rate-Limit Response

When `allow()` returns `false` (severity = error), the handler wrapper sends:

```
HTTP 429 Too Many Requests
Retry-After: 60
Cache-Control: no-store
```

Body: `"Too Many Requests"`.

## 3. State Machine

No internal state machine. Each `RateLimiter` instance operates as a stateless
token-bucket evaluator over a set of per-client entries. An individual entry has two
implicit states:

- **Tracked** — the entry slot is active and associated with a client IP.
- **Free** — the entry slot is unused and available for a new client.

Entries transition from free to tracked on first request from an IP, and transition
back to free when they expire past a time-to-live (5 minutes of inactivity). The
bucket itself is replenished lazily on every `allow()` call.

## 4. Data Flow

### 4.1 Request Admission

1. An HTTP request arrives on a rate-gated endpoint (OTA update trigger or config
   save).
2. The handler wrapper extracts the client's IPv4 address from the TCP connection.
3. The wrapper calls `allow(ip)` on the endpoint's assigned `RateLimiter` instance.
4. The limiter looks up the entry for the client IP (or creates a new one if absent).
5. If tokens are available (at least 1 full token), the limiter decrements the token
   count, resets the consecutive-hit counter, and returns `true`. The underlying
   handler is invoked.
6. If tokens are exhausted, the limiter increments the consecutive-hit counter and
   returns `false`. The wrapper responds with HTTP 429.

### 4.2 Token Refill

Tokens accrue continuously based on elapsed wall-clock time:

```
refilled = (elapsed_ms × ratePerMin × TOKEN_SCALE) / 60000
```

where `TOKEN_SCALE` provides sub-token precision (100 sub-tokens per unit token).
Refilled tokens are capped at the burst ceiling for the limiter instance.

### 4.3 Entry Management

- **New entry:** When a client IP is not yet tracked and a free slot exists, a new
  entry is created with the full burst token allocation.
- **Table full:** When all 32 slots are occupied by active entries, the oldest entry
  (by last-refill timestamp) is evicted and reused for the new IP.
- **TTL expiry:** Entries inactive for more than 5 minutes are lazily evicted during
  the next `allow()` call, freeing their slot.

## 5. Configuration Integration

The token-bucket parameters are compile-time constants, not schema-driven configuration
fields. Two independent limiter instances are instantiated at system bring-up:

| Limiter | Rate (tokens/min) | Burst ceiling | Scope |
|---|---|---|---|
| OTA Rate Limiter | 5 | 10 | Firmware update endpoints |
| Config Rate Limiter | 30 | 60 | Configuration persistence endpoints |

The OTA limiter is intentionally more restrictive to prevent flash wear from repeated
update attempts. The config limiter allows a higher burst to accommodate form
submissions with many fields, while still capping the sustained rate.

The severity policy (disabled / advisory / warning / error) is determined at build
time. The current build enforces the `error` severity for both endpoint groups.

## 6. Lifecycle

1. **Construction (bring-up):** Both limiter instances (`OTA Rate Limiter` and
   `Config Rate Limiter`) are initialized at static initialization time, before the
   main setup sequence runs. All 32 entry slots are set to free, and each entry's
   token count is seeded to the burst ceiling.
2. **Operation (per HTTP request):** Each incoming request on a gated endpoint
   triggers an `allow(ip)` evaluation, which performs lazy token refill and TTL
   eviction.
3. **Teardown:** No explicit teardown. The limiters hold only static memory.

## 7. Error Handling

| Condition | Handling |
|---|---|
| Tokens available | Token consumed, `hits` reset, request allowed |
| Tokens exhausted | `hits` incremented, request denied (HTTP 429) |
| Entry table full, new IP | Oldest entry (by timestamp) evicted; new entry created |
| Entry TTL expired (> 5 min inactive) | Entry marked free during next `allow()` call |
| IP not tracked in `getHits()` | Returns 0 |

## 8. Timing Constraints

- All limiter evaluations execute on core 0 within the AsyncWebServer task context
  (priority 10 via AsyncTCP). No separate FreeRTOS task is spawned.
- Token refill is computed from millisecond-resolution wall-clock time (`millis()`).
- The entry TTL for lazy eviction is 5 minutes (300,000 ms).
- The `Retry-After` hint in the HTTP 429 response is fixed at 60 seconds.
- An `allow()` call iterates the 32-entry table twice: once for IP lookup, once for
  TTL eviction. This is O(32) per request — negligible at HTTP request rates.
- No blocking delays are used in the evaluation path.

## 9. Memory and Allocation Model

Fully static. Zero heap allocation.

| Component | Size | Allocation |
|---|---|---|
| Entry table per limiter | 32 × 20 bytes = 640 bytes | Static BSS |
| Two limiter instances | 2 × 640 + overhead | Static (global scope) |
| `Entry` struct fields |  | `ip` (4 B), `tokens` (4 B), `lastMs` (4 B), `hits` (1 B), `used` (1 B) |

No PSRAM usage. No dynamic memory.

## 10. Safety Considerations

- **Core isolation:** All rate-limiter evaluations run on core 0, never preempting
  the core-1 DMX transmit task. A flood of HTTP requests cannot interfere with DMX
  break/mark timing.
- **Non-blocking admission:** The limiter returns immediately with a deny decision
  when the bucket is empty — it never blocks or stalls the AsyncWebServer event
  loop.
- **Bounded memory:** The fixed 32-entry table prevents unbounded memory growth
  under IP-spoofing or NATted-client storms. The total footprint is under 1.3 KB.
- **Flash-wear protection (OTA):** The restrictive 5/min limit on OTA endpoints
  prevents an attacker from repeatedly initiating firmware writes and exhausting
  flash write-endurance cycles.
- **Lazy eviction:** Expired entries are cleaned on the next request rather than via
  a timer task, avoiding the need for a dedicated sweep and its associated wakeups.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| net.web-server | consumes | Applies the limiter as a wrapper around rate-gated HTTP handler registrations |
| net.web-routes | consumes (indirectly) | The rate-gated handlers it implements (OTA and config) are wrapped by the limiter at registration time |
| sys.tasks | provides runtime | AsyncTCP event task on core 0 provides the execution context for all limiter calls |
| platformio.ini | provides build flags | AsyncTCP stack size, queue depth, and core-pinning build flags define the execution environment |

## 12. Testing Verification

No host-native unit tests cover the rate limiter module.

The limiter behavior is exercised indirectly through the Playwright E2E test suite,
which drives burst HTTP requests against the gated endpoints and verifies that
excess requests receive an HTTP 429 response with the expected `Retry-After` and
`Cache-Control: no-store` headers.

**Untested paths:**
- Token-bucket refill formula accuracy under long elapsed-time windows.
- Full-table eviction policy (oldest-by-timestamp) under 32 distinct client IPs.
- The `getHits()` counter interface has no test coverage.
- The `advisory` and `warning` severity modes are not exercised; only `error` is
  enforced in the current build.

## 13. Open Questions

1. Whether the `Retry-After` value (fixed at 60 seconds) should be dynamic, based on
   the estimated time until the next token becomes available under the configured
   refill rate.
2. Whether the severity policy should be runtime-configurable (e.g., via a schema
   field) rather than a compile-time constant, allowing operators to switch between
   `error` and `advisory` modes without a rebuild.
3. Whether the 32-entry table size was empirically tuned for the expected concurrent
   client count, or chosen as a round number.
4. Whether the `hits` counter should expose a diagnostic metric (e.g., via the
   WebSocket status push or `/info.json`) for operator visibility.
5. Whether per-request token granularity (1 token per request) is sufficient, or
   whether some endpoints (e.g., config import with large payloads) should consume
   a weighted number of tokens.

## 14. History

No recorded changes.
