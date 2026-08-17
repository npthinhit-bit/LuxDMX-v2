# Rate Limiter — Technical Reference

| Attribute | Value |
|---|---|
| **Layer** | `net` |
| **Module** | `rate_limiter` |
| **Source Files** | `src/net/rate_limiter.h`, `src/net/rate_limiter.cpp` |
| **Consumers** | [Web Server](net-web-server.md), [Web Routes](net-web-routes.md) |
| **Related** | [Web Server](net-web-server.md) |

## 1. File Inventory

| Source File | Purpose |
|---|---|
| `src/net/rate_limiter.h` | `RateLimiter` class declaration, `RateLimitEntry` struct, constants, global instance declarations |
| `src/net/rate_limiter.cpp` | `RateLimiter` constructor, `findOrCreate`, `allow`, `getHits` implementations, global instances |

## 2. Algorithm

IP-based token-bucket rate limiter. Called from core 0 (AsyncWebServer task) for every HTTP request that modifies state (config save, OTA initiation).

## 3. Constants

| Constant | Value | Source |
|---|---|---|
| `TOKEN_SCALE` | 100 (sub-token precision) | `src/net/rate_limiter.h:18` |
| `MAX_RATE_LIMIT_ENTRIES` | 32 | `src/net/rate_limiter.h:19` |
| `RATE_LIMIT_TTL_MS` | 300,000 (5 minutes) | `src/net/rate_limiter.h:20` |

## 4. Data Structures

### 4.1 `RateLimitEntry` (`src/net/rate_limiter.h:10-16`)
```cpp
struct RateLimitEntry {
    uint32_t ip;       // client IP (network byte order) — src/net/rate_limiter.h:11
    uint32_t tokens;   // current token count (scaled by TOKEN_SCALE) — src/net/rate_limiter.h:12
    uint32_t lastMs;   // last refill timestamp (ms) — src/net/rate_limiter.h:13
    uint8_t  hits;     // consecutive over-limit count — src/net/rate_limiter.h:14
    bool     used;     // entry is active — src/net/rate_limiter.h:15
};
```

### 4.2 `RateLimiter` Class (`src/net/rate_limiter.h:22-40`)
```cpp
class RateLimiter {
public:
    RateLimiter(int ratePerMin, int burst);           // src/net/rate_limiter.h:24
    bool allow(uint32_t ip);                          // src/net/rate_limiter.h:28
    uint8_t getHits(uint32_t ip);                     // src/net/rate_limiter.h:31
private:
    RateLimitEntry entries[MAX_RATE_LIMIT_ENTRIES];  // src/net/rate_limiter.h:34
    int  ratePerMin;                                 // src/net/rate_limiter.h:35
    int  burst;                                      // src/net/rate_limiter.h:36
    int  curSize;                                    // src/net/rate_limiter.h:37
    RateLimitEntry* findOrCreate(uint32_t ip, uint32_t nowMs);  // src/net/rate_limiter.h:39
};
```

## 5. Global Instances

Defined at `src/net/rate_limiter.cpp:87-88`:
```cpp
RateLimiter g_otaRateLimiter(5, 10);     // 5 requests/min, burst 10
RateLimiter g_configRateLimiter(30, 60); // 30 requests/min, burst 60
```
- Declared `extern` in `src/net/rate_limiter.h:43-44`.
- `g_otaRateLimiter` applied to `POST /ota/github` and `POST /ota/url` (`src/net/web_server.cpp:57,60`).
- `g_configRateLimiter` applied to `POST /config` and `POST /config/import` (`src/net/web_server.cpp:40,44`).

## 6. Constructor

Source: `src/net/rate_limiter.cpp:4`

- Initializes all 32 entries to `used = false`, `ip = 0`, `tokens = burst * TOKEN_SCALE`, `lastMs = 0`, `hits = 0` (`src/net/rate_limiter.cpp:6-12`).
- Sets `ratePerMin`, `burst`, `curSize = 0` (`src/net/rate_limiter.cpp:5`).

## 7. `findOrCreate` Method

Source: `src/net/rate_limiter.cpp:15`

```
findOrCreate(ip, nowMs):
    for i in 0..31:
        if entries[i].used && entries[i].ip == ip:
            return &entries[i]                     // found existing — src/net/rate_limiter.cpp:17-19
    for i in 0..31:
        if !entries[i].used:
            entries[i].used = true
            entries[i].ip = ip
            entries[i].tokens = burst * TOKEN_SCALE
            entries[i].lastMs = nowMs
            entries[i].hits = 0
            if i >= curSize: curSize = i + 1
            return &entries[i]                     // new entry — src/net/rate_limiter.cpp:21-30
    // table full: evict oldest by lastMs
    oldest = nowMs, oldestIdx = 0
    for i in 0..31:
        if entries[i].lastMs < oldest:
            oldest = entries[i].lastMs, oldestIdx = i
    // reuse oldest entry — src/net/rate_limiter.cpp:32-44
    entries[oldestIdx] = {ip, burst*SCALE, nowMs, 0, true}
    return &entries[oldestIdx]
```

## 8. `allow` Method

Source: `src/net/rate_limiter.cpp:47`

```
allow(ip):
    nowMs = millis()                            // src/net/rate_limiter.cpp:48
    e = findOrCreate(ip, nowMs)                 // src/net/rate_limiter.cpp:50
    elapsed = nowMs - e.lastMs                  // src/net/rate_limiter.cpp:52
    if elapsed > 0:
        refilled = (elapsed * ratePerMin * TOKEN_SCALE) / 60000  // src/net/rate_limiter.cpp:54
        if refilled > 0:
            e.tokens += refilled
            if e.tokens > burst * TOKEN_SCALE: e.tokens = burst * TOKEN_SCALE  // src/net/rate_limiter.cpp:55-58
            e.lastMs = nowMs                   // src/net/rate_limiter.cpp:59
    // lazy eviction of expired entries — src/net/rate_limiter.cpp:63-67
    for i in 0..31:
        if entries[i].used && (nowMs - entries[i].lastMs > RATE_LIMIT_TTL_MS):
            entries[i].used = false
    if e.tokens >= TOKEN_SCALE:                // src/net/rate_limiter.cpp:69
        e.tokens -= TOKEN_SCALE               // src/net/rate_limiter.cpp:70
        e.hits = 0                            // src/net/rate_limiter.cpp:71
        return true                           // allowed — src/net/rate_limiter.cpp:72
    e.hits++                                  // src/net/rate_limiter.cpp:74
    return false                              // denied — src/net/rate_limiter.cpp:75
```

### 8.1 Token Refill Formula
```
refilled = (elapsed_ms × ratePerMin × TOKEN_SCALE) / 60000
```
- For OTA limiter (5/min): refill = `(elapsed_ms × 5 × 100) / 60000` = `elapsed_ms / 12` tokens per ms.
- For config limiter (30/min): refill = `(elapsed_ms × 30 × 100) / 60000` = `elapsed_ms / 2` tokens per ms.

## 9. `getHits` Method

Source: `src/net/rate_limiter.cpp:78`

- Returns the `hits` counter for a given IP, or 0 if the IP is not tracked.
- Used for diagnostics — not checked in error responses.

## 10. Rate Limit Response

When `allow()` returns `false`, the `rateLimitHandler` wrapper in `src/net/web_server.cpp:13-24` sends:

```
HTTP 429 Too Many Requests
Retry-After: 60
Cache-Control: no-store
```

- Response text: `"Too Many Requests"` (`src/net/web_server.cpp:18`).

## 11. Call Sites

| Endpoint | RateLimiter | Source |
|---|---|---|
| `POST /config` | `g_configRateLimiter` | `src/net/web_server.cpp:40` |
| `POST /config/import` | `g_configRateLimiter` | `src/net/web_server.cpp:44` |
| `POST /ota/github` | `g_otaRateLimiter` | `src/net/web_server.cpp:57` |
| `POST /ota/url` | `g_otaRateLimiter` | `src/net/web_server.cpp:60` |

## 12. Thread Safety

- All `RateLimiter` methods are called from the AsyncWebServer task on **core 0** (`platformio.ini:42`).
- No locks or atomic operations are used — single-threaded access by design.
- `millis()` is safe to call from core 0 (`Arduino.h` wrapper).

## 13. Memory Model

| Component | Size | Allocation |
|---|---|---|
| `entries[]` | 32 × 20 bytes = 640 bytes | Static (constructor) |
| `RateLimitEntry` | 20 bytes | `src/net/rate_limiter.h:10-16` |
| Global instances | 2 × sizeof(RateLimiter) | Static (`src/net/rate_limiter.cpp:87-88`) |

- Zero heap allocation — all state in fixed-size arrays.
- `curSize` tracks high-water mark of used slots (for diagnostics, not critical path).

## 14. Timing

| Property | Value |
|---|---|
| TTL expiry check | Every `allow()` call (`src/net/rate_limiter.cpp:63-67`) |
| Token refill resolution | Millisecond (`millis()`) |
| Entry eviction policy | Oldest `lastMs` when table full (`src/net/rate_limiter.cpp:32-44`) |

## 15. Configuration

The rate limits are hardcoded at construction time in `src/net/rate_limiter.cpp:87-88`:

| Limiter | Rate | Burst | Scope |
|---|---|---|---|
| `g_otaRateLimiter` | 5 per minute | 10 | `POST /ota/github`, `POST /ota/url` |
| `g_configRateLimiter` | 30 per minute | 60 | `POST /config`, `POST /config/import` |

- OTA endpoints are more restrictive to prevent flash wear from repeated update attempts.
- Config endpoints allow burstier traffic (form submission with many fields) but cap sustained rate.

## 16. Error Handling

| Condition | Source | Behavior |
|---|---|---|
| IP not found | `getHits()` returns 0 | `src/net/rate_limiter.cpp:84` |
| Table full, no expired entries | Evict oldest by `lastMs` | `src/net/rate_limiter.cpp:32-44` |
| Tokens available | Return `true`, decrement tokens | `src/net/rate_limiter.cpp:69-72` |
| Tokens exhausted | Return `false`, increment `hits` | `src/net/rate_limiter.cpp:74-75` |

## 17. Performance

- `allow()` iterates all 32 entries twice: once for lookup, once for TTL eviction (`src/net/rate_limiter.cpp:16,63`).
- O(32) per request — negligible for HTTP request rates.
- No division in hot path — token refill uses integer math with `/ 60000` (`src/net/rate_limiter.cpp:54`).

## 18. Testing

- No host-native tests for `rate_limiter`.
- Could be tested off-target via the `native` test runner (`test/native/`) using `millis()` shim from `test/native/shim/`.

## 19. Integration

```
webRegisterRoutes (core 0)
    └─ rateLimitHandler(req, handler, rl)        src/net/web_server.cpp:13
        ├─ uint32_t ip = req->client()->remoteIP()  src/net/web_server.cpp:15
        ├─ if !rl.allow(ip):
        │     → HTTP 429 + Retry-After: 60        src/net/web_server.cpp:17-22
        └─ else: handler(req)
```

## 20. Known Limitations

- IP-based only — no per-user or token-based tracking.
- No burst reset after blocking — tokens continue to accrue during the 1-minute retry window.
- `curSize` is tracked but never used for eviction decisions (oldest-by-`lastMs` is used instead).
- `hits` counter is set but never read by application code.

## 21. References

- HTTP 429 response: [`src/net/web_server.cpp:13-24`](src/net/web_server.cpp#L13)
- Global instances: [`src/net/rate_limiter.cpp:87-88`](src/net/rate_limiter.cpp#L87)
- RateLimiter class: [`src/net/rate_limiter.h:22-40`](src/net/rate_limiter.h#L22)
- RateLimitEntry struct: [`src/net/rate_limiter.h:10-16`](src/net/rate_limiter.h#L10)
- Token refill formula: [`src/net/rate_limiter.cpp:54`](src/net/rate_limiter.cpp#L54)
- TTL eviction: [`src/net/rate_limiter.cpp:63-67`](src/net/rate_limiter.cpp#L63)
- AsyncTCP core pinning: [`platformio.ini:42`](platformio.ini#L42)
- Rate limiter consumed by: [Web Server](net-web-server.md), [Web Routes](net-web-routes.md)
