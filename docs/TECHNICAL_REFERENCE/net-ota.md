# OTA — Technical Reference

Domain: net.ota

## 1. Domain Scope

Owns the on-device execution of firmware over-the-air updates: the boot-retry crash
guard that detects a bricked image and resets NVS after `OTA_BOOT_TRIES`, the
HTTP multipart upload handler that streams a firmware blob into the ESP-IDF `Update`
partition, and the HTTP-GET streaming path for GitHub-release and arbitrary-URL fetch.
Delegates cryptographic signature verification to [net-ota-sign](./net-ota-sign.md).

Delegates:
- Signature verification to [net-ota-sign](./net-ota-sign.md) (`otaVerifyAndCommit`).
- Request-rate policing per client IP to [net-rate-limiter](./net-rate-limiter.md)
  (`g_otaRateLimiter`).
- OTA progress display to the web front-end ([net-web-routes](./net-web-routes.md)
  reads `otaProgPct` / `otaProgPhase`).
- Firmware version comparison to [sys-firmware-version](./sys-firmware-version.md)
  (`latestVersion`, `updateAvailable`).

Consumers:
- `src/main.cpp:78` — calls `otaBootUpdate()` during setup phase 5.
- `src/main.cpp:111` — calls `initOTA()` (empty stub) during setup phase 6.
- `src/net/web_server.cpp:56-58` — registers `/ota/github` POST → rate-limited
  `handleOtaGithub`.
- `src/net/web_server.cpp:59-61` — registers `/ota/url` POST → rate-limited
  `handleOtaUrl`.
- `src/net/web_server.cpp:64` — registers `/ota/upload` POST with
  `otaUploadChunk` as the upload-progress callback.

## 2. Layer Mapping

```
drv  →  cfg  →  core  →  net  →  app/sys
                         ↑
                    ota.cpp: otaBootUpdate,
                               otaUploadChunk,
                               otaFromGitHub,
                               otaFromUrl
                    └─ delegates verify to
                       [net-ota-sign] (net layer, same dir)
```

All code lives in the **net** layer (`src/net/ota.cpp`). The `Update` API and
`ESP.restart()` are Arduino-esp32 / ESP-IDF primitives; `otaVerifyAndCommit`
crosses into the **sys** layer via `sys_platform.h` globals.

## 3. Source Files

| File | Role |
|---|---|
| `src/net/ota.cpp` | `otaBootUpdate` (line 19), `otaUploadChunk` (line 43), `initOTA` (line 93), `otaFromGitHub` (line 96), `otaFromUrl` (line 159) |
| `src/net/ota.h` | Forward declarations: `otaBootUpdate`, `initOTA`, `otaFromGitHub`, `otaFromUrl` (lines 4-11) |
| `src/sys/sys_platform.h` | `OTA_BOOT_TRIES` constant (line 15), `extern` declarations for `otaProgPhase` (line 30), `otaProgPct` (line 31), `otaTarget` (line 35), `latestVersion` (line 36) |
| `src/sys/sys_platform.cpp` | Definitions: `otaTarget` (line 10), `latestVersion` (line 11), `otaProgPhase` (line 32), `otaProgPct` (line 33) |
| `src/net/rate_limiter.h` | `extern RateLimiter g_otaRateLimiter` declaration (line 43) |
| `src/net/rate_limiter.cpp` | `g_otaRateLimiter` instance: 5 req/min, burst 10 (line 87) |
| `src/net/ota_sign.h` | `otaVerifyAndCommit()` / `otaVerifySignature()` declarations (lines 7-8) |

## 4. Data Structures

### `otaProgPhase` — volatile uint8_t (`src/sys/sys_platform.h:30`, `src/sys/sys_platform.cpp:32`)

| Value | Meaning |
|---|---|
| 0 | Idle (no OTA in progress) |
| 1 | Downloading + writing to the update partition |
| 2 | Finalizing (verification committed, about to reboot) |
| 3 | Error (rate-limited, begin failed, signature failed, or commit failed) |

Declared in `src/sys/sys_platform.h:30` with the comment:
`// 0 idle, 1 downloading+writing, 2 finalizing, 3 error`.
Defined initialized to 0 at `src/sys/sys_platform.cpp:32`.

### `otaProgPct` — volatile uint8_t (`src/sys/sys_platform.h:31`, `src/sys/sys_platform.cpp:33`)

Progress percent (0–100) across the streamed firmware image. Written by the
upload/streaming callbacks, read by `handleOtaStatusJson`
(`src/net/web_routes.cpp:113,391`). Initialized to 0 at
`src/sys/sys_platform.cpp:33`.

### `otaTarget` — String (`src/sys/sys_platform.h:35`, `src/sys/sys_platform.cpp:10`)

URL of the firmware being fetched. Set by `handleOtaGithub`
(`src/net/web_routes.cpp:357,363`) and `handleOtaUrl`
(`src/net/web_routes.cpp:379`). Initialized to `"latest"` at
`src/sys/sys_platform.cpp:10`.

### `OTA_BOOT_TRIES` — `static const uint8_t` (`src/sys/sys_platform.h:15`)

Boot-retry cap = 3. Read by `otaBootUpdate()`
(`src/net/ota.cpp:25,35`).

## 5. Concurrency

Runs on **core 0** exclusively:

| Component | Core | Priority | Stack | Notes |
|---|---|---|---|---|
| `otaBootUpdate()` | core 0 (setup) | — | — | Runs synchronously in `setup()` before tasks spawn |
| `otaUploadChunk` | core 0 (AsyncWebServer task) | 10 (AsyncTCP) | — | Upload-progress callback, bounded by rate limiter |
| `otaFromGitHub` / `otaFromUrl` | core 0 (dedicated task) | 1 | 8192 B | Created by `handleOtaGithub` (`src/net/web_routes.cpp:368-371`) and `handleOtaUrl` (`src/net/web_routes.cpp:383-386`) |

The `volatile` qualifiers on `otaProgPhase` and `otaProgPct`
(`src/sys/sys_platform.h:30-31`) allow the web-handler task on core 0 to read
them while the OTA worker task writes them — both are on core 0, so no
cross-core barrier is needed, but `volatile` prevents compiler reordering.

## 6. State Machine

`otaUploadChunk` / `otaFromGitHub` drive the **phase** state machine via
`otaProgPhase` (`src/net/ota.cpp:49,53,64,74,80,87,98,106,112,117,132,145,150,155`):

| Phase | Entry action | Exit condition | Next phase |
|---|---|---|---|
| 0 — Idle | Default at boot reset | Upload starts or GitHub fetch begins | 1 |
| 1 — Downloading+writing | `Update.begin` done, streaming `Update.write` | Stream complete | 2 (verified) or 3 (error) |
| 2 — Finalizing | `Update.end(true)` succeeds, 100 ms delay | `ESP.restart()` fires | — (reboot) |
| 3 — Error | Any failure (rate, begin, write, verify, end) | Request returns 429/500 or function returns | 0 (after reboot) |

`otaBootUpdate` implements a separate NVS-backed boot-retry state machine
(`src/net/ota.cpp:19-41`):

| State | Condition | Action |
|---|---|---|
| `bootTries == 0` | Fresh boot | No-op (first boot after successful update) |
| `0 < bootTries < 3` | Recovery boot | Increment `boottry` in NVS, log retry |
| `bootTries >= 3` | Bricked image | Clear `boottry` to 0 (factory-reset of retry counter), return |

`OTA_BOOT_TRIES = 3` is the cap (`src/sys/sys_platform.h:15`).

## 7. Entry Points

| Entry point | Address | Caller | Notes |
|---|---|---|---|
| `otaBootUpdate()` | `src/net/ota.cpp:19` | `setup()` (`src/main.cpp:78`) | Runs in phase 5, before mDNS. Reads/writes NVS key `boottry` in namespace `dmxgw` |
| `initOTA()` | `src/net/ota.cpp:93` | `setup()` (`src/main.cpp:111`) | Empty stub — no initialization required |
| `otaUploadChunk` | `src/net/ota.cpp:43` | AsyncWebServer upload callback at `src/net/web_server.cpp:64` | Triggered by HTTP POST to `/ota/upload` |
| `handleOtaGithub` | `src/net/web_routes.cpp:352` | Route `/ota/github` POST (`src/net/web_server.cpp:56`) | Rate-limited, spawns core-0 task that calls `otaFromGitHub` (`src/net/web_routes.cpp:369`) |
| `handleOtaUrl` | `src/net/web_routes.cpp:374` | Route `/ota/url` POST (`src/net/web_server.cpp:59`) | Rate-limited, spawns core-0 task that calls `otaFromUrl` (`src/net/web_routes.cpp:384`) |
| `handleOtaStatusJson` | `src/net/web_routes.cpp:389` | Route `/ota/status` GET (`src/net/web_server.cpp:63`) | Returns `{"pct":..,"phase":..}` from `otaProgPct`/`otaProgPct` |
| `otaFromGitHub` | `src/net/ota.cpp:96` | Core-0 task spawned by `handleOtaGithub` (`src/net/web_routes.cpp:369`) | HTTP GET stream |
| `otaFromUrl` | `src/net/ota.cpp:159` | Core-0 task spawned by `handleOtaUrl` (`src/net/web_routes.cpp:384`) | Delegates to `otaFromGitHub` (`src/net/ota.cpp:160`) |

## 8. Data Flow

### Upload path (`otaUploadChunk`, `src/net/ota.cpp:43`)

1. **Index 0 (start)** — `src/net/ota.cpp:46`: `g_otaRateLimiter.allow(ip)` gates the
   client IP (`src/net/ota.cpp:48`); on reject, `otaProgPhase = 3`
   (`src/net/ota.cpp:49`) and HTTP 429 is returned
   (`src/net/ota.cpp:50`, `src/net/web_server.cpp:17-21`).
2. **Phase + begin** — `otaProgPhase = 1`, `otaProgPct = 0`
   (`src/net/ota.cpp:53-54`); `fwSize` read from
   `request->contentLength()` (`src/net/ota.cpp:55`);
   `Update.begin(fwSize, U_FLASH)` (`src/net/ota.cpp:56`); on failure
   `otaProgPhase = 3` + HTTP 500 (`src/net/ota.cpp:57-61`).
3. **Chunk write** — `Update.write(data, len)` per chunk
   (`src/net/ota.cpp:63`); on short write `otaProgPhase = 3`
   (`src/net/ota.cpp:64`); progress computed from
   `Update.progress()` / `Update.size()` (`src/net/ota.cpp:67-69`).
4. **Final (index N, `final == true`)** — if `OTA_SIGN_ENABLED`,
   call `otaVerifyAndCommit()` (`src/net/ota.cpp:73`); on failure
   `otaProgPhase = 3` + HTTP 500 (`src/net/ota.cpp:74-76`)
   ([see net-ota-sign](./net-ota-sign.md)).
5. **Commit** — `Update.end(true)` finalizes the flash write
   (`src/net/ota.cpp:79`); on success `otaProgPhase = 2`,
   `otaProgPct = 100`, log, HTTP 200, 100 ms delay, `ESP.restart()`
   (`src/net/ota.cpp:80-85`); on failure `otaProgPhase = 3` + HTTP 500
   (`src/net/ota.cpp:87-89`).

### Streaming path (`otaFromGitHub`, `src/net/ota.cpp:96`)

1. HTTP GET to `url` via `HTTPClient` (`src/net/ota.cpp:101-103`).
2. HTTP error (`httpCode <= 0`): `otaProgPhase = 3`, return
   (`src/net/ota.cpp:104-109`).
3. Zero-size body: `otaProgPhase = 3`, return
   (`src/net/ota.cpp:111-115`).
4. `Update.begin(fwSize, U_FLASH)` (`src/net/ota.cpp:116`); on failure
   `otaProgPhase = 3`, return (`src/net/ota.cpp:117-121`).
5. Read stream in 1024-byte chunks (`src/net/ota.cpp:124`) —
   `buf[1024]` stack-allocated; `Update.write(buf, r)` per chunk
   (`src/net/ota.cpp:131`); `otaProgPct = written * 100 / fwSize`
   (`src/net/ota.cpp:137`); `delay(1)` between chunks
   (`src/net/ota.cpp:139`).
6. Post-stream: signature verification via `otaVerifyAndCommit()`
   when `OTA_SIGN_ENABLED` (`src/net/ota.cpp:143-148`).
7. `Update.end(true)` (`src/net/ota.cpp:149`); success →
   `otaProgPhase = 2`, `otaProgPct = 100`, 100 ms delay,
   `ESP.restart()` (`src/net/ota.cpp:150-153`); failure →
   `otaProgPhase = 3` (`src/net/ota.cpp:155`).

`otaFromUrl` (`src/net/ota.cpp:159-160`) is an alias — it calls
`otaFromGitHub(url)` with no additional logic.

## 9. Protocol Layout

### HTTP multipart upload (`/ota/upload` POST, `src/net/web_server.cpp:64`)

| Parameter | Source | Description |
|---|---|---|
| Upload callback | `src/net/ota.cpp:43-91` | `otaUploadChunk` invoked by `AsyncWebServerUpload` machinery |
| `index` | HTTP multipart | Byte offset of `data` block (0 = first chunk) |
| `data` | HTTP multipart | Firmware bytes |
| `len` | HTTP multipart | Length of this chunk |
| `final` | HTTP multipart | `true` on the last chunk — triggers verify+commit |
| `contentLength` | HTTP header | Total firmware size for `Update.begin` |

### HTTP GET streaming (`/ota/github`, `/ota/url` POST → 302 redirect)

`handleOtaGithub` redirects the browser to `/ota`
(`src/net/web_routes.cpp:365-367`) then streams the image in a
background task. The client polls `/ota/status`
(`src/net/web_routes.cpp:389-395`) for `{"pct":..,"phase":..}`.

## 10. Config Integration

| Field | CFG flags | Schema line | Read in |
|---|---|---|---|
| `cfg.autoUpdate` | `CFG_REBOOT \| CFG_NOWEB` | `src/cfg/config_schema.cpp:131` | Not consumed by `ota.cpp` — read by `web_routes.cpp:296` (`handleAutoUpdatePost`), not wired to background fetch |
| `cfg.hostname` | `CFG_REBOOT \| CFG_KEEPNE` | `src/cfg/config_schema.cpp:47` | Not consumed by `ota.cpp` directly |
| `cfg.otaPassword` | `CFG_REBOOT \| CFG_SECRET \| CFG_KEEPNE` | `src/cfg/config_schema.cpp:49` | Present in schema but **not consumed** by the inspected OTA upload/streaming path — `otaUploadChunk` rate-limits by IP only (`src/net/ota.cpp:48`), no password check |

All config fields used by the OTA domain are `CFG_REBOOT` (require restart).
No config fields are written by `ota.cpp`.

## 11. Lifecycle

- **Init**: `initOTA()` (`src/net/ota.cpp:93`) — empty stub, called from
  `setup()` (`src/main.cpp:111`). No hardware resources allocated.
- **Boot guard**: `otaBootUpdate()` (`src/net/ota.cpp:19`) — reads NVS
  `boottry` counter, logs retry or triggers factory reset. Called from
  `setup()` (`src/main.cpp:78`) before mDNS registration.
- **Streaming**: `otaFromGitHub()` / `otaFromUrl()` (`src/net/ota.cpp:96,159`) —
  spawned as detached core-0 tasks, complete via `ESP.restart()` or
  return-on-error.
- **Progress polling**: `handleOtaStatusJson()`
  (`src/net/web_routes.cpp:389`) — polled by the browser while OTA is
  in-flight, reads `otaProgPct` / `otaProgPhase`.
- **Shutdown/reboot**: After successful `Update.end(true)`
  (`src/net/ota.cpp:79`), a 100 ms `delay` (`src/net/ota.cpp:84,152`) then
  `ESP.restart()` (`src/net/ota.cpp:85,153`). No graceful cleanup hook.

## 12. Error Handling

| Failure | Line | Behavior |
|---|---|---|
| Rate limit exceeded | `src/net/ota.cpp:48-52` | `otaProgPhase = 3`; HTTP 429 with `Retry-After: 60` and `Cache-Control: no-store` (applied by `rateLimitHandler` in `src/net/web_server.cpp:17-20`) |
| `Update.begin` fails | `src/net/ota.cpp:56-61` | `otaProgPhase = 3`; `Update.printError`; HTTP 500 "OTA begin failed" |
| `Update.write` short write | `src/net/ota.cpp:63-66` | `otaProgPhase = 3`; `Update.printError` logged |
| Signature verification fails | `src/net/ota.cpp:73-77` | `otaProgPhase = 3`; HTTP 500 "Signature verification failed"; rollback to running partition inside `otaVerifyAndCommit` (`src/net/ota_sign.cpp:120-123`) |
| `Update.end(true)` fails | `src/net/ota.cpp:87-89` | `otaProgPhase = 3`; `Update.printError`; HTTP 500 "Update failed" |
| HTTP GET fails (`httpCode <= 0`) | `src/net/ota.cpp:104-109` | `otaProgPhase = 3`; HTTP error string logged; return |
| Zero-size body | `src/net/ota.cpp:111-115` | `otaProgPhase = 3`; return |
| Stream read error (`r <= 0`) | `src/net/ota.cpp:130-135` | `otaProgPhase = 3`; `Update.printError`; break loop |
| Boot retry cap exceeded | `src/net/ota.cpp:25-31` | NVS `boottry` cleared to 0; returns to normal boot (factory-reset of retry counter) |
| Boot retry in progress | `src/net/ota.cpp:34-40` | `boottry` incremented in NVS; logs `boot retry N/3`; continues boot |

## 13. Memory Allocation

- `buf[1024]` (`src/net/ota.cpp:124`) — stack-local in `otaFromGitHub`,
  reused per chunk. Lives for the duration of the streaming task.
- `Update` API — writes to the ESP-IDF OTA data partition (flash-backed),
  not DRAM. Size requested via `Update.begin(fwSize, U_FLASH)`
  (`src/net/ota.cpp:56,116`).
- Task stack for GitHub/URL fetch: 8192 bytes (`src/net/web_routes.cpp:371,386`).
- No PSRAM or `heap_caps` allocation. No `MALLOC_CAP` flags.
- NVS access is transient: `Preferences` open/write/close per `otaBootUpdate`
  call (`src/net/ota.cpp:20-23,27-30,36-39`).

## 14. Timing

| Item | Value | Source |
|---|---|---|
| `OTA_BOOT_TRIES` cap | 3 boot attempts | `src/sys/sys_platform.h:15` |
| Post-success reboot delay | 100 ms | `src/net/ota.cpp:84,152` |
| Inter-chunk stream delay | 1 ms | `src/net/ota.cpp:139` |
| WiFi connect timeout (in `startWiFiStation`, not OTA-specific) | 30 000 ms | `src/net/net_state.cpp:131` |
| Link-local AutoIP retry timeout | 10 000 ms | `src/net/net_state.cpp:155` |
| Boot button hold threshold | 3 000 ms | `src/net/net_state.cpp:111` |
| Rate limiter: OTA | 5 req/min, burst 10 | `src/net/rate_limiter.cpp:87` |
| OTA worker task priority | 1 (idle priority) | `src/net/web_routes.cpp:371,386` |
| OTA worker task stack | 8192 bytes | `src/net/web_routes.cpp:371,386` |
| OTA worker task core | core 0 | `src/net/web_routes.cpp:371,386` |

## 15. Traceability

| Claim | File:line |
|---|---|
| `otaBootUpdate` reads NVS key `boottry` in namespace `dmxgw` | `src/net/ota.cpp:20-23` |
| `OTA_BOOT_TRIES` = 3 | `src/sys/sys_platform.h:15` |
| `otaBootUpdate` factory-reset path: clears `boottry` to 0 when >= cap | `src/net/ota.cpp:25-31` |
| `otaBootUpdate` increments + logs retry | `src/net/ota.cpp:34-40` |
| `otaUploadChunk` checks `g_otaRateLimiter.allow(ip)` at `index==0` | `src/net/ota.cpp:47-52` |
| `otaUploadChunk` 429 response sets `otaProgPhase=3` | `src/net/ota.cpp:49-50` |
| `otaUploadChunk` begins Update at `index==0` | `src/net/ota.cpp:55-61` |
| `otaUploadChunk` writes chunks via `Update.write` | `src/net/ota.cpp:63-66` |
| `otaUploadChunk` computes progress from `Update.progress`/`Update.size` | `src/net/ota.cpp:67-69` |
| `otaUploadChunk` verifies via `otaVerifyAndCommit` when `OTA_SIGN_ENABLED` | `src/net/ota.cpp:72-78` |
| `otaUploadChunk` finalizes via `Update.end(true)` then `ESP.restart` | `src/net/ota.cpp:79-90` |
| `initOTA` is an empty stub | `src/net/ota.cpp:93-94` |
| `otaFromGitHub` HTTP GET + `HTTPClient` | `src/net/ota.cpp:101-103` |
| `otaFromGitHub` HTTP error → phase 3 | `src/net/ota.cpp:104-109` |
| `otaFromGitHub` zero-size guard | `src/net/ota.cpp:111-115` |
| `otaFromGitHub` `Update.begin` | `src/net/ota.cpp:116` |
| `otaFromGitHub` 1024-byte `buf[1024]` | `src/net/ota.cpp:124` |
| `otaFromGitHub` streaming loop reads 1KB chunks | `src/net/ota.cpp:126-139` |
| `otaFromGitHub` signature verify gate | `src/net/ota.cpp:143-148` |
| `otaFromGitHub` finalize + reboot | `src/net/ota.cpp:149-156` |
| `otaFromUrl` delegates to `otaFromGitHub` | `src/net/ota.cpp:159-160` |
| `otaProgPhase` extern declaration | `src/sys/sys_platform.h:30` |
| `otaProgPct` extern declaration | `src/sys/sys_platform.h:31` |
| `OTA_BOOT_TRIES` defined | `src/sys/sys_platform.h:15` |
| `g_otaRateLimiter` extern declaration | `src/net/rate_limiter.h:43` |
| `g_otaRateLimiter` configured 5/min burst 10 | `src/net/rate_limiter.cpp:87` |
| `otaBootUpdate` called in setup phase 5 | `src/main.cpp:78` |
| `initOTA` called in setup phase 6 | `src/main.cpp:111` |
| `/ota/upload` route → `otaUploadChunk` | `src/net/web_server.cpp:64` |
| `/ota/github` route → rate-limited `handleOtaGithub` | `src/net/web_server.cpp:56-58` |
| `/ota/url` route → rate-limited `handleOtaUrl` | `src/net/web_server.cpp:59-61` |
| `handleOtaGithub` spawns core-0 task calling `otaFromGitHub` | `src/net/web_routes.cpp:368-371` |
| `handleOtaUrl` spawns core-0 task calling `otaFromUrl` | `src/net/web_routes.cpp:383-386` |
| `handleOtaStatusJson` returns `{"pct":..,"phase":..}` | `src/net/web_routes.cpp:389-395` |
| `otaProgPct` read by version JSON | `src/net/web_routes.cpp:113` |
| `OTA_SIGN_ENABLED` defaults to 1 in ota.cpp | `src/net/ota.cpp:15-17` |
| Rate-limit 429 response adds `Retry-After: 60` + `Cache-Control: no-store` | `src/net/web_server.cpp:17-20` |
| `otaPassword` schema entry | `src/cfg/config_schema.cpp:49` |
| `autoUpdate` schema entry, `CFG_NOWEB` | `src/cfg/config_schema.cpp:131` |
| `cfg.hostname` schema entry | `src/cfg/config_schema.cpp:47` |
| Boot button hold detection | `src/net/net_state.cpp:108-113` |
| WiFi connect 30 s timeout | `src/net/net_state.cpp:131` |

## 16. Cross-References

- [net-ota-sign](./net-ota-sign.md) — `otaVerifyAndCommit()` (`src/net/ota.cpp:73,144`)
  performs Ed25519 signature verification and rollback.
- [net-rate-limiter](./net-rate-limiter.md) — `g_otaRateLimiter`
  (`src/net/rate_limiter.h:43`, `src/net/rate_limiter.cpp:87`) gates the
  upload and streaming triggers.
- [net-web-server](./net-web-server.md) — route registration table
  (`src/net/web_server.cpp:56-64`); `rateLimitHandler` wrapper
  (`src/net/web_server.cpp:13-24`).
- [net-web-routes](./net-web-routes.md) — `handleOtaGithub`,
  `handleOtaUrl`, `handleOtaStatusJson`, `handleVersionJson`
  (`src/net/web_routes.cpp:352-395,106-116`).
- [sys-firmware-version](./sys-firmware-version.md) — `latestVersion`,
  `updateAvailable` globals consumed by `handleVersionJson`
  (`src/net/web_routes.cpp:111-112`).
- [sys-tasks](./sys-tasks.md) — `versionCheckTask`
  (`src/sys/tasks.cpp:172-176`) polls GitHub for releases every 60 s;
  `versionCheck()` (`src/sys/firmware_version.cpp:18`) sets
  `updateAvailable`.
- [config-engine](./config-engine.md) — `cfg.autoUpdate`
  (`src/cfg/config_schema.cpp:131`) toggles auto-update flag
  (`src/net/web_routes.cpp:296`).
- [docs/ota-key-management.md](../ota-key-management.md) — full key
  generation, embedding, signing, and rotation procedure.

## 17. Limitations

- `otaPassword` exists in the config schema (`src/cfg/config_schema.cpp:49`)
  but is **not checked** by any OTA upload or streaming path — `otaUploadChunk`
  rate-limits by client IP only (`src/net/ota.cpp:48`).
- `cfg.autoUpdate` (`src/cfg/config_schema.cpp:131`) is `CFG_NOWEB` — it does
  not appear on the `/config` web form — and no background auto-fetch loop
  consumes it; the flag can only be toggled via `/autoupdate`
  (`src/net/web_routes.cpp:295-299`).
- `otaFromUrl` is an alias with no distinct logic (`src/net/ota.cpp:159-160`)
  — URL and GitHub fetch paths are identical, so URL-specific error
  differentiation is not possible.
- `Update.begin(fwSize > 0 ? fwSize : UPDATE_SIZE_UNKNOWN, ...)` uses
  `UPDATE_SIZE_UNKNOWN` when the HTTP response lacks a `Content-Length`
  header (`src/net/ota.cpp:56`); this prevents the `Update` class from
  pre-erasing the correct flash region, degrading to incremental erase.
- The 100 ms `delay` before `ESP.restart()`
  (`src/net/ota.cpp:84,152`) blocks the core-0 task; no async watchdog kick
  during this window.
- No cancellation mechanism — once `otaFromGitHub` starts, the background task
  cannot be aborted; a power cycle or watchdog reset is the only way to stop
  an in-flight streaming fetch.

## 18. Open Questions

- Not determinable from the inspected source code — whether `otaPassword`
  was intended to gate the upload handler; it is defined in the schema
  (`src/cfg/config_schema.cpp:49`) but no password check exists in
  `otaUploadChunk` or `handleOtaGithub`/`handleOtaUrl`.
- Not determinable from the inspected source code — whether
  `config_schema.cpp:131`'s `autoUpdate` flag is meant to trigger an
  automatic fetch after `versionCheck()` detects `updateAvailable` — no
  code path from `versionCheck()` (`src/sys/firmware_version.cpp:18`) to
  `otaFromGitHub` was found.
- Not determinable from the inspected source code — the exact definition of
  `rateLimitHandler` (referenced as `src/net/web_server.cpp:13`) is not
  inspected; only the route registrations at `src/net/web_server.cpp:56-64`
  are confirmed.

## 19. Testing

No unit test or native test coverage for the OTA module. The
`test/native/` suite (`config_test`, `seqlock_test`, `merge_test`,
`rdm_types_test`) does not reference `ota.cpp`, `ota_sign.cpp`, or
`sign_ota.py`. The `tools/sign_ota.py` script is a host-side Python tool
that requires the `cryptography` library (not tested by the native build).

Validation relies on the 5-minute firmware evaluation workflow in
`CLAUDE.md` (serial log + `/info.json` and `/version.json` web endpoints),
and on the Ed25519 signature round-trip (sign with
`tools/sign_ota.py`, verify with `otaVerifyAndCommit`).

## 20. History

- Boot-retry crash guard (`otaBootUpdate`) moved from the v1 monolith into
  `src/net/ota.cpp:19`; NVS counter namespace is `"dmxgw"`
  (`src/net/ota.cpp:21`), cap `OTA_BOOT_TRIES = 3`
  (`src/sys/sys_platform.h:15`).
- Rate limiting added: IP-based token-bucket via `g_otaRateLimiter`
  (5 req/min, burst 10) wrapping `/ota/github`, `/ota/url`, and
  `/ota/upload` (`src/net/ota.cpp:48`, `src/net/web_server.cpp:56-61`).
- Signature verification split into `src/net/ota_sign.cpp`
  (`otaVerifySignature`, `otaVerifyAndCommit`) from the original
  `otaUploadChunk` logic; `OTA_SIGN_ENABLED` compile-time flag gates
  verification on all envs. Production build for
  `esp32s3_n16r8_eth` leaves signing enabled (no `-DOTA_SIGN_ENABLED=0`
  in `platformio.ini:167`); dev envs set `-DOTA_SIGN_ENABLED=0`
  (`platformio.ini:73,95,116,155`).
- `otaFromUrl` added as an alias for `otaFromGitHub`
  (`src/net/ota.cpp:159-160`).
