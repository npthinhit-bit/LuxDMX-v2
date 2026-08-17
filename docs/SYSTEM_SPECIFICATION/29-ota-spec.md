# OTA — System Specification

Domain: net.ota

## 1. Module Overview

The OTA (Over-the-Air) Update subsystem manages the end-to-end lifecycle of firmware
updates delivered to the device: from receiving an update trigger over HTTP, through
streaming or uploading the firmware image into the ESP-IDF OTA update partition,
verifying its cryptographic signature, committing it to flash, and rebooting into
the new image.

The subsystem provides three install paths, all of which converge on the same
streaming-and-verification pipeline:

- **GitHub release fetch** — an HTTP GET stream downloads a firmware release asset
  from a GitHub release URL.
- **Arbitrary URL fetch** — an HTTP GET stream downloads a firmware image from any
  user-supplied URL (delegates to the same streaming path as GitHub).
- **Local chunked upload** — an HTTP multipart POST uploads a firmware image in
  chunks directly into the update partition.

After the image is written, the OTA Sign module (same layer, sibling component)
performs Ed25519 signature verification. If verification passes, the image is
committed and the device reboots. If verification fails, the boot partition is rolled
back to the currently running application so a bad image can never boot.

A separate boot-retry crash guard detects when the newly booted image fails to
stabilize and resets the retry counter after a configurable number of attempts,
preventing a permanently bricked device.

**Owns:** the three OTA install paths, the update-partition streaming writer, the
boot-retry crash guard, and the phase/progress reporting interface.
**Delegates to:** OTA Sign (Ed25519 signature verification and partition rollback),
Rate Limiter (per-IP admission control on the fetch endpoints), Firmware Version
(provides the latest-version check consumed by the version-check task), Config Engine
(defines `autoUpdate` and `otaPassword` schema fields).
**Consumed by:** Web Server (route registrations), Web Routes (handler implementations
that invoke the install paths).

## 2. External Interfaces

### 2.1 HTTP Endpoints

| Endpoint | Method | Purpose | Rate Limited |
|---|---|---|---|
| `/ota/github` | POST | Initiate a firmware fetch from a GitHub release URL | Yes (OTA Rate Limiter) |
| `/ota/url` | POST | Initiate a firmware fetch from an arbitrary URL | Yes (OTA Rate Limiter) |
| `/ota/upload` | POST | Receive a firmware image via HTTP multipart chunked upload | Yes (OTA Rate Limiter) |
| `/ota/status` | GET | Poll OTA progress: returns `{"pct": 0–100, "phase": 0–3}` | No |

### 2.2 Phase / Progress Reporting

The subsystem exposes two volatile observables consumed by the Web Routes status
handler:

| Observable | Type | Domain | Description |
|---|---|---|---|
| `phase` | uint8 | 0–3 | 0 = Idle, 1 = Downloading + writing, 2 = Finalizing (commit, about to reboot), 3 = Error |
| `pct` | uint8 | 0–100 | Progress percentage across the streamed firmware image |
| `target` | String | — | URL of the firmware being fetched (for diagnostics) |

### 2.3 Boot-Retry Constants

| Constant | Value | Description |
|---|---|---|
| `OTA_BOOT_TRIES` | 3 | Maximum consecutive boot attempts before the retry counter is factory-reset |

### 2.4 Entry Points

| Entry point | Caller | Purpose |
|---|---|---|
| `otaBootUpdate()` | System bring-up (setup) | Reads the NVS-backed boot-retry counter; logs recovery boots or clears the counter if the cap is exceeded |
| `initOTA()` | System bring-up (setup) | No-op initialization stub (no resources to allocate) |
| `handleOtaGithub()` | Web Server route handler | Parses the POST body for a version/URL, spawns the streaming worker |
| `handleOtaUrl()` | Web Server route handler | Parses the POST body for a URL, spawns the streaming worker |
| `handleOtaStatusJson()` | Web Server route handler | Returns the current `phase` and `pct` as JSON |
| `otaUploadChunk()` | AsyncWebServer upload callback | Receives HTTP multipart chunks; at offset 0 it performs rate-limit admission and begins the Update session |

## 3. State Machine

The subsystem operates a four-phase state machine driven by the `phase` observable:

| Phase | Entry action | Exit condition | Next phase |
|---|---|---|---|
| 0 — Idle | Default at boot | Upload starts or fetch begins | 1 |
| 1 — Downloading + writing | Update session begun; streaming `write` calls in progress | Stream complete (all bytes received) | 2 (verified) or 3 (error) |
| 2 — Finalizing | Signature verification passed; `Update.end(true)` committed the partition | `ESP.restart()` fires | — (reboot) |
| 3 — Error | Any failure (rate-limited, begin failed, short write, signature failed, end failed) | Handler returns 429/500 or worker function returns | 0 (after reboot) |

A separate boot-retry state machine is persisted in NVS:

| State | Condition | Action |
|---|---|---|
| Fresh boot | Boot-retry counter is 0 | No-op |
| Recovery boot | Counter is 1–2 | Increment counter, log "boot retry N/3" |
| Bricked | Counter >= 3 | Clear counter to 0 (factory-reset of retry cap), proceed with normal boot |

## 4. Data Flow

### 4.1 Upload Path (Local Chunked Upload)

1. The first upload chunk (index 0) arrives. The client IP is extracted and the
   OTA Rate Limiter is consulted. If denied, `phase` is set to Error and an
   HTTP 429 is returned.
2. If allowed, `phase` is set to Downloading (1) and `pct` to 0. The firmware
   image size is read from the HTTP `Content-Length` header.
3. `Update.begin(fwSize, U_FLASH)` initializes the OTA update partition. On failure,
   `phase` is set to Error and HTTP 500 is returned.
4. Each subsequent chunk is written via `Update.write(data, len)`. If a short write
   occurs, `phase` is set to Error.
5. Progress is computed from `Update.progress() / Update.size()` and reported via
   `pct`.
6. On the final chunk (`final == true`), if signature verification is enabled,
   `otaVerifyAndCommit()` is invoked. On failure, `phase` is set to Error and
   HTTP 500 is returned.
7. `Update.end(true)` finalizes the flash write. On success, `phase` is set to
   Finalizing (2), `pct` to 100, and a 100 ms delay precedes `ESP.restart()`.
   On failure, `phase` is set to Error and HTTP 500 is returned.

### 4.2 Streaming Path (GitHub / URL Fetch)

1. A dedicated low-priority core-0 task is spawned to perform the HTTP GET stream.
2. `HTTPClient` issues a GET to the firmware URL. If the HTTP response code is
   non-positive, `phase` is set to Error and the function returns.
3. If the response body size is zero, `phase` is set to Error and the function
   returns.
4. `Update.begin(fwSize, U_FLASH)` prepares the update partition. On failure,
   `phase` is set to Error and the function returns.
5. The response body is streamed in 1 KB chunks. Each chunk is written via
   `Update.write(buf, len)`. Progress is reported via `pct`.
6. After the stream completes, if signature verification is enabled,
   `otaVerifyAndCommit()` is invoked.
7. `Update.end(true)` finalizes the partition. On success, `phase` is set to
   Finalizing (2), `pct` to 100, a 100 ms delay, then `ESP.restart()`.
   On failure, `phase` is set to Error.

### 4.3 Boot-Retry Path

1. During system bring-up, `otaBootUpdate()` reads the boot-retry counter from NVS
   (namespace `dmxgw`, key `boottry`).
2. If the counter is 0, the boot is the first since a successful update — no action.
3. If the counter is between 1 and `OTA_BOOT_TRIES - 1`, the counter is incremented
   and the boot is logged as a recovery attempt.
4. If the counter has reached the cap, the counter is cleared to 0 (preventing a
   permanent brick) and normal boot proceeds.

### 4.4 Cross-Module Delegation

- The streaming/upload workers call `otaVerifyAndCommit()` after writing all image
  bytes. This delegates to the OTA Sign module, which performs Ed25519 verification
  and, on failure, rolls back the boot partition to the running application.
- Both fetch endpoints are gated by the OTA Rate Limiter at the HTTP handler
  registration level.
- The version-check background task (System layer) polls for firmware releases every
  60 seconds and sets an `updateAvailable` flag that the web UI consumes.

## 5. Configuration Integration

| Config Field | Apply Semantics | Usage in OTA |
|---|---|---|
| `autoUpdate` | Reboot | Toggles the background auto-update flag; consumed by the Web Routes auto-update handler, not by the streaming install paths directly |
| `hostname` | Reboot | Not consumed by the OTA module directly |
| `otaPassword` | Reboot, Secret | Present in the config schema but NOT checked by any OTA install path — admission is IP rate-limited only |

The rate limits themselves are enforced by the Rate Limiter module (5 requests/min with
a burst of 10 for OTA endpoints), not via schema-driven configuration.

The signature verification gate is controlled by a compile-time build flag
(`OTA_SIGN_ENABLED`):

| Build Profile | `OTA_SIGN_ENABLED` |
|---|---|
| Dev / single-board targets | 0 (disabled) |
| Production / 4-universe Ethernet target | 1 (enabled) |

## 6. Lifecycle

1. **Init (setup phase 5):** `otaBootUpdate()` runs synchronously during bring-up,
   before mDNS registration. It reads and (if needed) updates the NVS boot-retry
   counter.
2. **Init (setup phase 6):** `initOTA()` is called — a no-op stub requiring no
   resource allocation.
3. **Idle:** The phase observable rests at 0. The system operates normally.
4. **Update initiated (core 0):** An HTTP request on `/ota/github` or `/ota/url`
   spawns a low-priority task that streams the image. A request on `/ota/upload`
   drives the upload callback chain.
5. **Streaming:** `phase` is 1. Image bytes are written to the OTA update partition
   via the ESP-IDF Update framework. Progress is reported via `pct`.
6. **Verification:** The OTA Sign module verifies the Ed25519 signature. On success,
   `Update.end(true)` commits the partition and the device reboots. On failure,
   the boot partition is rolled back.
7. **Reboot:** A 100 ms delay precedes `ESP.restart()`. On the next boot,
   `otaBootUpdate()` runs again to detect whether the new image stabilized.
8. **No teardown:** The OTA worker completes via reboot or error return.

## 7. Error Handling

| Failure | Behavior |
|---|---|
| Rate limit exceeded | `phase` set to Error (3); HTTP 429 with `Retry-After: 60` and `Cache-Control: no-store` |
| Update session begin fails | `phase` set to Error; HTTP 500 "OTA begin failed" |
| Short write during transfer | `phase` set to Error; error logged |
| Signature verification fails | `phase` set to Error; HTTP 500 "Signature verification failed"; boot partition rolled back to running app |
| Update session commit fails | `phase` set to Error; HTTP 500 "Update failed" |
| HTTP GET fails on streaming path | `phase` set to Error; error string logged; function returns |
| Zero-size response body | `phase` set to Error; function returns |
| Stream read error mid-transfer | `phase` set to Error; loop breaks; error logged |
| Boot retry cap exceeded | NVS retry counter cleared to 0; normal boot resumes (no brick) |
| Boot retry in progress | NVS retry counter incremented; recovery boot logged; boot continues |

## 8. Timing Constraints

| Item | Value |
|---|---|
| Boot-retry cap | 3 consecutive boot attempts |
| Post-success reboot delay | 100 ms |
| Inter-chunk stream delay | 1 ms |
| OTA worker task priority | 1 (idle priority) |
| OTA worker task stack | 8,192 bytes |
| OTA worker task core | Core 0 |
| WiFi connect timeout (not OTA-specific) | 30,000 ms |
| Rate limiter: OTA endpoints | 5 requests/min, burst 10 |

The streaming worker runs at idle priority (1) on core 0, yielding to all other
tasks including the web server and serial console. The 100 ms pre-reboot delay
blocks only the OTA worker task, not the DMX transmit path (core 1).

Verification (SHA-256 hashing + Ed25519) scales linearly with image size. For a
~500 KB image, the hashing loop reads 1 KB chunks from flash, producing approximately
500 iterations. There is no hard deadline for verification — it runs to completion
on the core-0 OTA task.

## 9. Memory and Allocation Model

- The streaming worker uses a stack-allocated 1,024-byte buffer (`buf[1024]`),
  reused per chunk for the duration of the fetch.
- The ESP-IDF Update framework writes firmware image data directly to the OTA flash
  partition — no DRAM allocation for the image body.
- The update-partition size is requested at `Update.begin(fwSize)` time. When the
  HTTP response lacks a `Content-Length` header, `UPDATE_SIZE_UNKNOWN` is used,
  which prevents pre-erasing the correct flash region and degrades to incremental
  erase.
- No PSRAM allocation. No `heap_caps` or `MALLOC_CAP` usage.
- NVS access for the boot-retry counter is transient: open, read/write, close per
  call.

## 10. Safety Considerations

- **Core isolation:** All OTA execution occurs on core 0, never preempting the
  core-1 DMX transmit task. A long-running firmware download or verification cannot
  corrupt DMX break/mark timing.
- **Crash guard:** The boot-retry counter (persistent in NVS) detects a bricked
  image that fails to stabilize post-reboot. After 3 failed boots, the counter is
  reset, allowing the device to fall back to the last known-good partition.
- **Rollback on signature failure:** The OTA Sign module rolls the boot partition
  back to the currently running application when verification fails, ensuring a
  tampered or corrupt image can never boot.
- **Rate limiting:** The OTA Rate Limiter prevents rapid-fire update attempts that
  could exhaust flash write-endurance or starve the web server.
- **No cancellation:** Once a streaming fetch begins, the worker task cannot be
  aborted. Power-cycling or a watchdog reset is the only way to interrupt an
  in-flight download.
- **Non-blocking phase report:** The `phase` and `pct` observables are `volatile`
  and readable by the web-handler task, allowing the browser to poll progress
  without blocking.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| net.ota-sign | downstream dependency | Verifies Ed25519 signature after streaming; rolls back partition on failure |
| net.rate-limiter | upstream guard | `g_otaRateLimiter` gates all three install paths at the HTTP handler level |
| net.web-server | upstream consumer | Registers the `/ota/github`, `/ota/url`, `/ota/upload`, `/ota/status` routes |
| net.web-routes | upstream peer | Implements `handleOtaGithub`, `handleOtaUrl`, `handleOtaStatusJson` which invoke the install paths |
| sys.firmware-version | upstream | Provides `latestVersion` and `updateAvailable` consumed by the browser and version-check task |
| sys.tasks | upstream scheduler | `versionCheckTask` polls GitHub for releases every 60 seconds |
| cfg.config-engine | upstream | Defines `autoUpdate`, `otaPassword`, and `hostname` schema fields |
| platformio.ini | upstream build | `OTA_SIGN_ENABLED` build flag controls whether verification is compiled in |

## 12. Testing Verification

No host-native unit tests cover the OTA module. The `test/native/` suite
(`config_test`, `seqlock_test`, `merge_test`, `rdm_types_test`) does not reference
the OTA install paths.

Validation relies on:

- The 5-minute firmware evaluation workflow: flash a build, monitor serial logs for
  successful WiFi + Art-Net/sACN initialization, verify the web interface renders,
  and confirm no task-watchdog resets or Guru Meditations.
- The Playwright E2E suite drives HTTP requests against a live device to verify
  error responses, rate-limit (429) behavior, and the `/ota/status` JSON shape.
- The Ed25519 signature round-trip: sign with the host-side signing tool, flash,
  verify the device accepts (or rejects) the image and boots (or rolls back).

**Untested paths:**
- The boot-retry crash guard state machine (NVS counter read/write, factory-reset
  on cap exceed).
- Partition rollback on signature failure (requires a deliberately bad-signed image).
- Streaming-fetch error paths (HTTP failure, zero-size body, mid-stream read error).
- The 100 ms pre-reboot delay blocking window.

## 13. Open Questions

1. Whether `otaPassword` (present in the config schema) was intended to gate the
   upload handler. It is defined as a schema field but no password check exists in
   any OTA install path — admission is IP rate-limited only.
2. Whether `autoUpdate` is meant to trigger an automatic fetch after the
   version-check task detects an available update. No code path was found from the
   version-check task to `otaFromGitHub`.
3. Whether the HTTP 429 `Retry-After` value (fixed at 60 seconds) should be dynamic.
4. Whether a cancellation mechanism should be added to abort an in-flight streaming
   fetch, rather than requiring a power cycle or watchdog reset.
5. Whether the 100 ms pre-reboot delay should be replaced with an async watchdog
   kick to avoid blocking the core-0 task during that window.

## 14. History

- Boot-retry crash guard introduced: NVS-backed counter in the `dmxgw` namespace,
  capped at 3 consecutive attempts. On cap exceed, the counter is factory-reset
  to prevent permanent bricking.
- Rate limiting added: IP-based token-bucket via the OTA Rate Limiter (5 req/min,
  burst 10) wrapping all three install paths.
- Signature verification split into a dedicated sibling module (OTA Sign) for
  Ed25519 verification and partition rollback.
- `otaFromUrl` added as an alias for `otaFromGitHub` — both fetch paths share the
  identical streaming logic.
- `UPDATE_SIZE_UNKNOWN` fallback used when the HTTP response lacks a
  `Content-Length` header, degrading to incremental flash erase.
