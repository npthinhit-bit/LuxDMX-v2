# Firmware Version Specification

Domain: sys.firmware-version

## 1. Module Overview

The Firmware Version module owns the device firmware identity and the asynchronous GitHub releases check. It exposes three compile-time identity constants: the semver version string, a build timestamp, and a variant identifier. It performs a live HTTP GET to the GitHub releases API to determine whether a newer firmware release is available. The result is stored in global flags that the web user interface reads to display an update banner.

The version check runs in a low-priority background task at a 60-second cadence, blocking for at most 8 seconds on the HTTP request. It does not perform the actual OTA install, which is delegated to the OTA module, but it populates the global flags that gate the update notification and the install target.

Owned: the compile-time version constants, the GitHub releases HTTP check, the update-available flag and latest-version string globals.
Delegated to: OTA module (install path triggered by user action).
Consumed by: Web routes (/info and /config JSON), Web frontend (HTML version placeholder), System bring-up (mDNS service text).

## 2. External Interfaces

### Caller-Facing API

| Function | Arguments | Behaviour |
|---|---|---|
| versionCheck | (void) | If WiFi is connected, performs an HTTP GET to the GitHub releases latest API. Parses the response for the release tag. If the tag differs from the compiled version, sets the update-available flag and stores the remote tag string. Silent on all failure modes. |

### Compile-Time Identity Constants

| Constant | Type | Value | Description |
|---|---|---|---|
| FIRMWARE_VERSION | const char array | 0.0.0-dev | Semver string; matches the git tag at release time. Dev builds use a -dev suffix. Updated by a version-setting script. |
| FIRMWARE_BUILD | const char array | compiler timestamp | Build timestamp (date and time) baked in by the compiler. |
| FIRMWARE_VARIANT | const char array | luxdmx_4uni | Compile-time variant identifier string. |

### Runtime Globals

| Symbol | Type | Initial | Set by | Read by |
|---|---|---|---|---|
| updateAvailable | boolean | false | versionCheck (when remote tag differs from local version) | Web routes, web frontend |
| otaTarget | string | latest | never set by versionCheck (stays latest) | OTA module install path |
| latestVersion | string | empty | versionCheck (parsed remote tag) | /info endpoint |

### Board Identity Constants

| Symbol | Source | Values |
|---|---|---|
| BOARD_ID | compile-time flags | luxdmx_v6, wt32eth01, esp32s3-devkitc-1, or esp32-devkitc |
| MCU_ID | compile-time flags | esp32s3 or esp32 |

### GitHub API

The check target is the GitHub releases latest endpoint. The repository URL can be overridden at compile time via a build flag; otherwise it defaults to the project official repository.

| Condition | URL |
|---|---|
| Repository override flag defined | https://api.github.com/repos/<override>/releases/latest |
| Default | https://api.github.com/repos/thinhh0321/LuxDMX/releases/latest |

## 3. State Machine

Minimal two-state model:

- **Idle**: updateAvailable is false and latestVersion is empty. This is the initial state after setup completes and before the first check runs. It is also the state when no newer release is found or the check fails.
- **Update available**: updateAvailable is true and latestVersion holds the remote release tag. Set when versionCheck reads a tag that differs from the compiled firmware version.

There is no explicit checking state flag. The 60-second periodic cadence means overlap between one check and the next is impossible: the maximum HTTP blocking time (8 seconds) is well under the 60-second interval.

## 4. Data Flow

1. **Guard**: If the WiFi radio is not connected, versionCheck returns immediately without making a network request.

2. **HTTP GET**: An HTTP client is created with an 8-second timeout. The request is issued as a GET to the GitHub releases latest endpoint, with an Accept header specifying the GitHub JSON content type. HTTPS is used (the GitHub API requires TLS via the BearSSL client in the Arduino-esp32 stack).

3. **Status check**: If the HTTP response code is not 200, the client is cleaned up and versionCheck returns without modifying the globals.

4. **Size check**: If the response content length is zero or exceeds 64 KB, the request is aborted and the function returns. This guards against runaway or empty response bodies.

5. **Streaming parse**: The response body is read in 1024-byte chunks. The parser scans each chunk for the literal tag_name key. When found, it extracts the value following the colon, strips the opening quote character, and removes a leading v prefix if present. The extracted value is stored in a 32-byte local buffer.

6. **Store**: The parsed tag string is assigned to latestVersion. If it differs from the compiled FIRMWARE_VERSION, the updateAvailable flag is set to true.

7. **Cleanup**: The HTTP client is released.

8. **Consume**: The /info web route emits latestVersion and updateAvailable in its JSON response. The web frontend reads updateAvailable to display an update notification banner.

## 5. Configuration Integration

None. The version constants are compiled into the firmware binary. The board identity and variant string are determined by compile-time build flags. The updateAvailable flag and latestVersion string are sys-layer runtime globals: they are not persisted to NVS or exposed as configuration fields. No runtime configuration field influences the version check behaviour.

## 6. Lifecycle

- **Init phase (core 0, setup)**: No explicit init call. The first version check occurs after the task scheduler starts and the version-check task fires.
- **Periodic**: The version-check task runs at priority 1 (lowest), with a 12288-byte stack sufficient to hold the HTTP client, the 1024-byte read buffer, and the 32-byte tag parse buffer. The period is 60000 ms. The task fires immediately on creation and then every 60 seconds.
- **Shutdown**: None; the task loops forever; the HTTP client is released after each iteration.

## 7. Error Handling

| Condition | Behaviour |
|---|---|
| WiFi not connected | versionCheck returns immediately; no globals modified. |
| HTTP response code not 200 | HTTP client cleaned up; function returns; no globals modified. |
| Content length is zero | HTTP client cleaned up; function returns; no globals modified. |
| Content length exceeds 64 KB | HTTP client cleaned up; function returns; no globals modified. |
| HTTP read returns no data | Read loop terminates; function returns; no globals modified. |
| tag_name key not found in response | Function returns; no globals modified; updateAvailable stays false. |
| Parsed version matches compiled version | updateAvailable stays false; latestVersion is still updated. |

All failure modes are silent: no error logging, no serial output, no error flags. The update-available state simply remains false until a successful check finds a newer release. The 8-second HTTP timeout is well under the 60-second task period, so a hanging or slow response from the GitHub API cannot delay the next scheduled check.

## 8. Timing Constraints

| Item | Value |
|---|---|
| Check period | 60000 ms (60 seconds) |
| HTTP timeout | 8000 ms (8 seconds) |
| Read chunk size | 1024 bytes |
| Maximum response size | 65536 bytes (64 KB) |
| Task priority | 1 (lowest) |
| Task stack | 12288 bytes |

The 8-second HTTP timeout is well under the 60-second period, ensuring a slow or unresponsive GitHub API call cannot overlap with the next scheduled check. The task runs at the lowest priority and never preempts the real-time DMX or RDM paths.

## 9. Memory and Allocation Model

- FIRMWARE_VERSION, FIRMWARE_BUILD, FIRMWARE_VARIANT, and the GitHub API URL are compile-time constant character arrays stored in read-only data memory.
- The HTTP client is allocated on the Arduino heap during each check and released after the request completes.
- A 1024-byte buffer is stack-allocated within the version-check task for streaming the response body. It lives within the task 12288-byte stack for the duration of the HTTP read loop.
- A 32-byte character array is stack-allocated for holding the parsed release tag.
- The latestVersion and otaTarget globals are Arduino String objects on the heap, reassigned on each successful check.

## 10. Safety Considerations

- The version-check task runs at priority 1 (lowest), unpinned. It never preempts the real-time DMX transmit task (priority 19) or the RDM task (priority 18) on core 1.
- The 8-second HTTP timeout bounds the worst-case blocking time. Combined with the 60-second period, a hung network request cannot starve the lower-level diagnostic tasks.
- The latestVersion and updateAvailable globals are written by the low-priority task and read by the web server core-0 callback context. These are non-volatile, non-atomic globals; a concurrent read during a String reassignment could observe a partially-constructed value. The 60-second period makes this a low-probability race, but it is not fully eliminated.
- The GitHub releases API is accessed over HTTPS. The TLS stack is used by the Arduino HTTP client, but no certificate pinning or fingerprint verification is performed in the inspected path: the TLS stack relies on the standard root certificate store. A compromised root store could allow a man-in-the-middle attack to inject a false release tag.
- Network failures (DNS, TLS, HTTP errors) leave updateAvailable as false. The device does not downgrade or install based on a failed check.
- There is no retry or back-off logic. A transient 5xx error from the GitHub API results in the check failing until the next 60-second cycle. This is acceptable because the check is purely informational: it does not trigger any action.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| Task Scheduling | downstream (consumer) | The version-check task calls versionCheck every 60 seconds. |
| Web Routes | downstream (consumer) | The /info endpoint reads latestVersion and updateAvailable for its JSON response. |
| Web Frontend | downstream (consumer) | The HTML template has a placeholder substituted with the version string; the JavaScript reads updateAvailable to show an update banner. |
| OTA | downstream (consumer) | The otaTarget global (latest) is read by the OTA install path when the user triggers an update. |
| System bring-up | downstream (consumer) | The mDNS service advertises the firmware version as a service text record. |
| WiFi / Network | upstream (producer of connectivity) | versionCheck requires WiFi to be connected before making the HTTP request. |

## 12. Testing Verification

No host-native test covers the version-check function; the HTTP client, WiFi status, and TLS stack are not shimmed in the native test environment. The compile-time version constants are baked in at build time and not separately tested. The 60-second periodic check is validated live by observing the /info web endpoint version and board fields. The tag-name parser leading-v stripping behaviour would ideally be unit-tested with a canned JSON response body, but no host test exists for this.

## 13. Open Questions

1. Whether a build-time repository override flag is ever defined in the build configuration; the default GitHub repository is used in all inspected environments.
2. Whether latestVersion is consumed by any auto-update polling path beyond the manual OTA install flow; the OTA module full install sequence was not inspected.
3. Whether updateAvailable is explicitly reset to false on any code path other than the next versionCheck iteration finding a matching version.

## 14. History

The version identity constants were moved from the main setup file into a dedicated module during the five-layer architecture rewrite, exposing them through extern declarations instead of preprocessor defines. The versionCheck function was added to replace the static unknown version shown in the original v1 firmware, using the GitHub releases latest endpoint for O(1) latest-tag lookup. The version-check task was created at the lowest priority with a 12288-byte stack to accommodate the HTTP client, the 1024-byte read buffer, and the 32-byte tag parse buffer on the ESP32-S3. The board identity constants were extracted into a platform header to keep the version header includable from C-language components. The FIRMWARE_VARIANT is currently hardcoded to luxdmx_4uni regardless of the build environment, meaning dev builds for other targets still report the 4-universe variant string.
