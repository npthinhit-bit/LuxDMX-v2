# Firmware Version — Technical Reference

Domain: sys.firmware-version

## 1. Domain Scope

Owns the compile-time firmware identity constants (`FIRMWARE_VERSION`, `FIRMWARE_BUILD`, `FIRMWARE_VARIANT`) and the asynchronous GitHub-releases check (`versionCheck`) that populates the `updateAvailable` / `latestVersion` / `otaTarget` globals so the web UI can show an update banner. The version strings are `const char[]` baked at compile time; `versionCheck` performs a live HTTP GET to the GitHub `releases/latest` API.

Consumers:
- `src/main.cpp:82` — `FIRMWARE_VERSION` used as mDNS service text `fw-version`.
- `src/net/web_routes.cpp:88-90,108-110` — version/build/variant emitted in the `/info` and `/config` JSON.
- `src/frontend/web_frontend.cpp:51,59` — `__FWVER__` placeholder substituted into the web UI HTML.
- `[sys-tasks](./sys-tasks.md)` — `versionCheckTask` calls `versionCheck()` every 60 000 ms (`src/sys/tasks.cpp:174`).
- `src/main.cpp:82` and `[sys-tasks](./sys-tasks.md):81` — boot + periodic check cadence.

## 2. Layer Mapping

```
drv  →  cfg  →  core  →  net  →  app/sys
                              ↑
                         versionCheck()
                         firmware_version.cpp
                         (HTTP GET → GitHub)
                              ↓
                         sys_platform.h
                         (updateAvailable,
                          latestVersion,
                          otaTarget)
```

The module lives in the **sys** layer. It reads `WiFi` status (ESP32 Arduino) and `cfg.hostname`-adjacent identity fields, and writes to the **sys** boundary globals declared in `src/sys/sys_platform.h`. The OTA install path it triggers is in `net/ota.cpp` (see [net-ota](../net-ota.md) for the full install flow).

## 3. Source Files

| File | Role |
|---|---|
| `src/sys/firmware_version.cpp` | `FIRMWARE_VERSION` (line 7), `FIRMWARE_BUILD` (line 8), `FIRMWARE_VARIANT` (line 9), `GH_RELEASES_URL` (lines 12-16), `versionCheck` (line 18) |
| `src/sys/firmware_version.h` | `FIRMWARE_VERSION`/`FIRMWARE_BUILD`/`FIRMWARE_VARIANT` extern decls (lines 6-8), `versionCheck` decl (line 11) |
| `src/sys/sys_platform.h` | `BOARD_ID` (line 11), `updateAvailable` (line 32), `otaTarget` (line 35), `latestVersion` (line 36) — globals populated/observed by `versionCheck` |
| `src/sys/sys_platform.cpp` | `BOARD_ID` definitions per `-D` board flag (lines 13-21), `MCU_ID` (lines 23-27), `updateAvailable = false` (line 34), `otaTarget = "latest"` / `latestVersion = ""` (lines 10-11) |
| `src/main.cpp` | mDNS `fw-version` text (line 82) |
| `src/net/web_routes.cpp` | `/info` JSON emits version/build/variant/board/mcu (lines 88-101) |
| `src/frontend/web_frontend.cpp` | `__FWVER__` HTML substitution (lines 51, 59) |
| `src/sys/tasks.cpp` | `versionCheckTask` calls `versionCheck` (line 174), task created prio 1 / 12 288 B stack (line 81) |

## 4. Data Structures

### Compile-time constants (`src/sys/firmware_version.cpp:7-9`)

| Constant | Type | Value | Description |
|---|---|---|---|
| `FIRMWARE_VERSION` | `const char[]` | `"0.0.0-dev"` | Semver string; matches git tag at release time; `x.x.x-dev` for dev builds. Updated by `scripts/set_version.sh`. |
| `FIRMWARE_BUILD` | `const char[]` | `__DATE__ " " __TIME__` | Build timestamp baked by the compiler. |
| `FIRMWARE_VARIANT` | `const char[]` | `"luxdmx_4uni"` | Compile-time variant string. |

### Runtime globals (consumed by `versionCheck`, declared in `src/sys/sys_platform.h`)

| Name | Type | Initial | Set by | Read by |
|---|---|---|---|---|
| `updateAvailable` | `bool` | `false` (`src/sys/sys_platform.cpp:34`) | `versionCheck` line 73 | web UI / routes |
| `otaTarget` | `String` | `"latest"` (`src/sys/sys_platform.cpp:10`) | not set by `versionCheck` (stays `"latest"`) | `[net-ota](../net-ota.md)` install path |
| `latestVersion` | `String` | `""` (`src/sys/sys_platform.cpp:11`) | `versionCheck` line 71 | `/info` JSON (`src/net/web_routes.cpp:108`) |

### `BOARD_ID` (`src/sys/sys_platform.cpp:13-21`)

| Condition | Value |
|---|---|
| `BOARD_LUXDMX_V6` defined | `"luxdmx_v6"` |
| `HAS_ETH_RMII` or `HAS_WIRED_ETH` defined | `"wt32eth01"` |
| `CONFIG_IDF_TARGET_ESP32S3` defined | `"esp32s3-devkitc-1"` |
| else (ESP32 generic) | `"esp32-devkitc"` |

`MCU_ID` (`src/sys/sys_platform.cpp:23-27`) = `"esp32s3"` or `"esp32"`.

### `GH_RELEASES_URL` (`src/sys/firmware_version.cpp:12-16`)

| Condition | URL |
|---|---|
| `GITHUB_REPO` defined | `"https://api.github.com/repos/" GITHUB_REPO "/releases/latest"` |
| else | `"https://api.github.com/repos/thinhh0321/LuxDMX/releases/latest"` |

## 5. Concurrency

**Single call site, runs in a background task (prio 1).**

- `versionCheck()` is called only from `versionCheckTask` (`src/sys/tasks.cpp:174`), a 12 288-byte-stack task at priority 1, unpinned (`src/sys/tasks.cpp:81,172-176`).
- It writes `latestVersion` and `updateAvailable` (`src/sys/sys_platform.h:32,36`) — plain `bool`/`String` globals with no `volatile` qualifier. They are read by the web-route JSON builder on core 0 (`src/net/web_routes.cpp:88-110`) from the AsyncWebServer context. Since `String` assignment is not atomic and there is no lock, a concurrent read during an HTTP request could observe a partially-constructed string — see [Limitations](#17-limitations).
- `versionCheck` blocks up to 8 000 ms on the HTTP GET (`src/sys/firmware_version.cpp:22`) — but runs at prio 1 so it never preempts the DMX/RDM path (priorities 5 and 19).

## 6. State Machine

Minimal two-state:

- **Idle**: `updateAvailable = false`, `latestVersion = ""` — the initial state after `setup()` before the first check, or when no newer release is found.
- **Update available**: `updateAvailable = true`, `latestVersion = <remote tag>` — set when `versionCheck` reads a tag differing from `FIRMWARE_VERSION` (`src/sys/firmware_version.cpp:71-73`). The web UI reads `updateAvailable` to show the banner.

No explicit "checking" state flag; the 60 s periodic cadence means overlap between one check and the next is impossible (8 s max HTTP timeout vs 60 s period).

## 7. Entry Points

| Entry point | Address | Caller |
|---|---|---|
| `versionCheck()` | `src/sys/firmware_version.cpp:18` | `versionCheckTask` (`src/sys/tasks.cpp:174`), every 60 000 ms |
| `versionCheckTask` | `src/sys/tasks.cpp:172` | FreeRTOS scheduler (prio 1, 12 288 B stack, `src/sys/tasks.cpp:81`) |

## 8. Data Flow

1. **Guard** — if `WiFi.status() != WL_CONNECTED`, return immediately (`src/sys/firmware_version.cpp:19`).
2. **HTTP GET** — create `HTTPClient`, set 8 000 ms timeout (`src/sys/firmware_version.cpp:22`), begin `GH_RELEASES_URL` (`src/sys/firmware_version.cpp:23`), add `Accept: application/vnd.github+json` header (`src/sys/firmware_version.cpp:24`), issue `GET` (`src/sys/firmware_version.cpp:25`).
3. **Status check** — if response code != 200, `http.end()` and return (`src/sys/firmware_version.cpp:26-29`).
4. **Size check** — reject if `len == 0` or `len > 65536` (`src/sys/firmware_version.cpp:32-35`) — guards against runaway bodies.
5. **Streaming parse** — read the body in 1024-byte chunks (`src/sys/firmware/version.cpp:38-46`), scanning for the literal `"tag_name"` (10 bytes, `memcmp` at `src/sys/firmware_version.cpp:49`), then extracting the value after the colon, stripping the opening `"` and a leading `v` (`src/sys/firmware_version.cpp:51-59`).
6. **Store** — `latestVersion = String(latest)` (`src/sys/firmware_version.cpp:71`); if it differs from `FIRMWARE_VERSION`, set `updateAvailable = true` (`src/sys/firmware/firmware_version.cpp:72-73`).
7. **Cleanup** — `http.end()` after the loop (`src/sys/firmware_version.cpp:68`).
8. **Consume** — web `/info` JSON emits `latestVersion` (`src/net/web_routes.cpp:108`); `updateAvailable` drives the update banner.

## 9. Protocol Layout

The GitHub `releases/latest` API response is JSON; `versionCheck` does **not** use a JSON parser — it scans the raw body for the `"tag_name"` key. The relevant fragment:

```json
{ "tag_name": "v1.2.3", "name": "...", "prerelease": false, ... }
```

| Parsed element | Offset approach | Code |
|---|---|---|
| `"tag_name"` literal | `memcmp(buf+i, "\"tag_name\"", 10)` | `src/sys/firmware_version.cpp:49` |
| value after `:` | linear scan past spaces/colon (`src/sys/firmware_version.cpp:52`) |
| skip opening `"` | `if (buf[v] == '"') v++` (`src/sys/firmware_version.cpp:54`) |
| strip leading `v` | `if (buf[v] == 'v' && k == 0) { v++; continue; }` (`src/sys/firmware_version.cpp:57`) |

The HTTP request itself is a standard GET to `api.github.com` (HTTPS — `HTTPClient` uses the BearSSL client under the hood in Arduino-esp32; no certificate pinning is observed in the inspected source).

## 10. Config Integration

None. `versionCheck` does not read any `Config` field. `FIRMWARE_VERSION`/`VARIANT` are compile-time constants; `BOARD_ID`/`MCU_ID` are compile-time `-D` selections. `latestVersion` and `updateAvailable` are sys-layer globals, not persisted to NVS.

## 11. Lifecycle

- **Init (core 0, `setup()`):** no explicit call; the first check happens after `createTasks()` spawns `versionCheckTask` (`src/main.cpp:130`), which fires immediately then every 60 s (`src/sys/tasks.cpp:173-176`).
- **Periodic:** 60 000 ms cadence, prio 1 (`src/sys/tasks.cpp:81,175`).
- **Shutdown:** None — the task loops forever; `http.end()` is called per-iteration.

## 12. Error Handling

| Condition | Value | Behaviour | Source |
|---|---|---|---|
| WiFi not connected | `WiFi.status() != WL_CONNECTED` | return immediately | `src/sys/firmware_version.cpp:19` |
| HTTP status != 200 | `code != 200` | `http.end()`, return | `src/sys/firmware_version.cpp:26-29` |
| Content-Length 0 | `len == 0` | `http.end()`, return | `src/sys/firmware_version.cpp:32` |
| Content-Length > 64 KB | `len > 65536` | `http.end()`, return | `src/sys/firmware_version.cpp:32` |
| `stream->read` <= 0 | `chunk <= 0` | break out of read loop | `src/sys/firmware_version.cpp:47` |
| `tag_name` not found | `!found || k == 0` | return (no update set) | `src/sys/firmware_version.cpp:69` |
| Version matches | `String(latest) == String(FIRMWARE_VERSION)` | `updateAvailable` stays false | `src/sys/firmware_version.cpp:72` |

All failures are **silent** — only the HTTP 200 path writes to the globals. No `ESP_LOGE`; no `Serial.printf` on error.

## 13. Memory Allocation

- `FIRMWARE_VERSION` / `FIRMWARE_BUILD` / `FIRMWARE_VARIANT` — `.rodata` static const char arrays (`src/sys/firmware_version.cpp:7-9`).
- `GH_RELEASES_URL` — `.rodata` `static const char*` (`src/sys/firmware_version.cpp:12-16`).
- `buf[1024]` — stack-local `uint8_t` in `versionCheck` (`src/sys/firmware/version.cpp:38`); lives within the 12 288-byte task stack.
- `latest[32]` — stack-local `char[32]` (`src/sys/firmware_version.cpp:41`); holds the parsed tag (max ~31 chars).
- `HTTPClient http` — Arduino heap-backed (`src/sys/firmware_version.cpp:21`); `http.end()` frees it each iteration.
- `latestVersion` / `otaTarget` — `String` globals on the Arduino heap (`src/sys/sys_platform.cpp:10-11`); reassigned on each check.

## 14. Timing

| Item | Value | Source |
|---|---|---|
| Check period | 60 000 ms | `src/sys/tasks.cpp:175` |
| HTTP timeout | 8 000 ms | `src/sys/firmware_version.cpp:22` |
| Read chunk size | 1024 bytes | `src/sys/firmware_version.cpp:38` |
| Max response size | 65 536 bytes | `src/sys/firmware_version.cpp:32` |
| Task priority | 1 | `src/sys/tasks.cpp:81` |
| Task stack | 12 288 bytes | `src/sys/tasks.cpp:81` |

The 8 s HTTP timeout is well under the 60 s period, so a slow/no-response GitHub call cannot overlap with the next scheduled check.

## 15. Traceability

| Claim | Evidence |
|---|---|
| `FIRMWARE_VERSION = "0.0.0-dev"` | `src/sys/firmware_version.cpp:7` |
| `FIRMWARE_BUILD = __DATE__ " " __TIME__` | `src/sys/firmware_version.cpp:8` |
| `FIRMWARE_VARIANT = "luxdmx_4uni"` | `src/sys/firmware_version.cpp:9` |
| Version updated by `scripts/set_version.sh` | `src/sys/firmware_version.h:5` |
| `GH_RELEASES_URL` default | `src/sys/firmware_version.cpp:13-16` |
| `versionCheck` guards on `WL_CONNECTED` | `src/sys/firmware_version.cpp:19` |
| HTTP 8 s timeout | `src/sys/firmware_version.cpp:22` |
| Non-200 / bad-length early returns | `src/sys/firmware_version.cpp:26-35` |
| Streaming 1024-byte read loop | `src/sys/firmware_version.cpp:38-47` |
| `memcmp ... "tag_name"` scan | `src/sys/firmware_version.cpp:49` |
| Leading `v` stripped | `src/sys/firmware_version.cpp:57` |
| `latestVersion` set | `src/sys/firmware_version.cpp:71` |
| `updateAvailable` set when mismatch | `src/sys/firmware_version.cpp:72-73` |
| `versionCheckTask` prio 1, 12 288 B stack | `src/sys/tasks.cpp:81` |
| `versionCheck` called every 60 s | `src/sys/tasks.cpp:174-175` |
| `BOARD_ID` per-compile-time flag | `src/sys/sys_platform.cpp:13-21` |
| `/info` emits version/build/variant/board/mcu | `src/net/web_routes.cpp:88-101` |
| `updateAvailable` initialized false | `src/sys/sys_platform.cpp:34` |
| `otaTarget = "latest"` initial | `src/sys/sys_platform.cpp:10` |

## 16. Cross-References

- `[sys-tasks](./sys-tasks.md)` — `versionCheckTask` (prio 1) calls `versionCheck` every 60 s (`src/sys/tasks.cpp:174`); task created in `createTasks()` (`src/sys/tasks.cpp:81`).
- `config-engine` — `BOARD_ID` is used in `/info` alongside `FIRMWARE_VERSION` (`src/net/web_routes.cpp:88-101`); hostname comes from `cfg.hostname` but version identity is compile-time.
- `[net-ota](../net-ota.md)` — `latestVersion` drives the web update banner; `otaTarget` defaults to `"latest"` and is consumed by the OTA install path.
- `sys-platform` — `src/sys/sys_platform.h` declares `updateAvailable`, `latestVersion`, `otaTarget`, `BOARD_ID`; `src/sys/sys_platform.cpp` defines them.

## 17. Limitations

- `latestVersion` and `updateAvailable` are **non-volatile, non-atomic** globals (`src/sys/sys_platform.h:32,36`) written by a prio-1 task and read by the core-0 AsyncWebServer handler — a concurrent HTTP request during a `String` reassignment could observe a torn value. The 60 s period makes this a low-probability but real data race.
- The `tag_name` scan is **not a JSON parser** — a response with `"tag_name"` appearing inside a `body` or `assets` field would be mis-parsed (`src/sys/firmware_version.cpp:48-63`). It works because GitHub places `tag_name` early in the object, but it is fragile.
- HTTPS is used (api.github.com) but **no certificate pinning** or fingerprint is configured in the inspected `versionCheck` — MITM risk if the device's root store is compromised.
- No retry/back-off on HTTP failure — a transient 5xx from GitHub leaves `updateAvailable = false` until the next 60 s cycle.
- `FIRMWARE_VARIANT` is hardcoded to `"luxdmx_4uni"` (`src/sys/firmware_version.cpp:9`) regardless of the build environment — a dev build for `esp32dev` still reports the 4-universe variant string.

## 18. Open Questions

1. Not determinable from the inspected source code — whether `GITHUB_REPO` is ever defined at build time (the `#if defined(GITHUB_REPO)` branch at `src/sys/firmware_version.cpp:12`); no `platformio.ini` `-DGITHUB_REPO=...` was found in the inspected environment.
2. Not determinable from the inspected source code — whether `latestVersion` is also consumed by the auto-update polling path (`cfg.autoUpdate`, `src/cfg/config_schema.cpp:131`); the OTA install flow ([net-ota](../net-ota.md)) was not inspected.
3. Not determinable from the inspected source code — whether `updateAvailable` is reset to `false` on any code path other than the next `versionCheck` mismatch; no explicit reset was found outside `src/sys/sys_platform.cpp:34` (initial).

## 19. Testing

- No host-native test covers `versionCheck` — `HTTPClient`, `WiFi`, and `millis` are not fully shimmed in `test/native/`.
- `config_test.cpp` does not test version identity or the GitHub check (`test/native/config_test.cpp` covers config field resolution only).
- The 60 s periodic check is validated live by observing the `/info` endpoint's `latestVersion` and `updateAvailable` fields (`src/net/web_routes.cpp:88-110`) after pointing the device at GitHub.
- The `tag_name` parser's leading-`v` stripping (`src/sys/firmware_version.cpp:57`) is a behaviour that would ideally be unit-tested with a canned JSON body, but no such host test exists.

## 20. History

- Version strings moved from `main.cpp` to `firmware_version.cpp`/`firmware_version.h` during the 5-layer split, exposing them via `extern const char[]` instead of `#define`.
- `versionCheck` added to replace the static `"unknown"` version shown in the original v1 firmware — the GitHub-releases API GET was chosen over the `releases` list endpoint for O(1) latest-tag lookup.
- `versionCheckTask` created at priority 1 with a 12 288-byte stack (`src/sys/tasks.cpp:81`) — the large stack accommodates the `HTTPClient` + 1024-byte read buffer + 32-byte tag parse buffer on the ESP32-S3.
- `BOARD_ID`/`MCU_ID` extracted into `sys_platform.cpp` (`src/sys/sys_platform.cpp:13-27`) from a previous `main.cpp` `#ifdef` block, to keep `firmware_version.h` includable from ESP-IDF C components.
