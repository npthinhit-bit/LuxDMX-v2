# Lessons Learned - LuxDMX-v2 ESP-IDF Migration

## Phase 0: Documentation and Planning

### Documentation Assessment
- **Lesson**: Comprehensive documentation enables confident system reconstruction
  - The existing `docs/` directory provides **complete coverage** of architecture, protocols, and implementation details
  - Black-box specifications allow implementation without coupling to legacy code
  - Technical reference documents serve as both design documents and implementation guides

- **Lesson**: Documentation quality directly impacts migration success
  - Well-structured specifications with clear cross-references accelerate understanding
  - Detailed timing constraints and performance measurements prevent integration issues
  - Explicit error handling documentation reduces debugging time

- **Pitfall**: Documentation gaps can create uncertainty
  - Missing implementation details (e.g., `sanitizeOutputs()`) require additional research
  - Undocumented edge cases may surface during integration
  - Open questions in specifications need resolution before implementation

### Project Planning
- **Lesson**: Phased migration reduces risk and enables incremental validation
  - Clear phase exit criteria provide measurable milestones
  - Functional parity requirements ensure no feature regression
  - Living documentation captures evolving architectural decisions

- **Lesson**: Hardware abstraction is critical for multi-board support
  - Board-specific configurations must be isolated from core logic
  - Pin mapping tables enable easy hardware adaptation
  - Peripheral abstraction interfaces simplify driver development

- **Pitfall**: Underestimating hardware-specific quirks
  - Different ESP32 variants have unique characteristics (PSRAM, brownout, etc.)
  - Peripheral availability varies between chip variants
  - Pin constraints differ significantly between boards

## Phase 1: WiFi + LED + Config

### WiFi Implementation
- **Lesson**: Robust WiFi provisioning requires multiple fallback mechanisms
  - Station mode with credential persistence provides reliable connection
  - SoftAP fallback with captive portal enables recovery from configuration errors
  - Exponential backoff prevents network storms during reconnection attempts

- **Lesson**: WiFi event handling must be non-blocking
  - Blocking operations in WiFi callbacks can cause system instability
  - Event-driven architecture enables responsive user interface
  - Separate WiFi manager task improves system stability

- **Pitfall**: WiFi operations can cause core 0 contention
  - Network stack operations should be isolated from time-critical tasks
  - WiFi event handlers must complete quickly to avoid system hangs
  - Memory allocation in WiFi callbacks can cause fragmentation

### LED Drivers
- **Lesson**: Hardware abstraction enables flexible status indication
  - Generic LED driver interface supports multiple LED types (GPIO, WS2812, panel)
  - Status patterns communicate system state effectively
  - FreeRTOS task ensures smooth pattern rendering

- **Lesson**: Brightness control should account for human perception
  - Linear brightness scaling appears uneven to human eyes
  - Logarithmic scaling provides better perceived brightness control
  - Gamma correction improves LED color accuracy

- **Pitfall**: Direct GPIO manipulation can cause flickering
  - Hardware-specific drivers should handle low-level timing
  - RMT peripheral provides better timing control for WS2812 LEDs
  - Interrupt-driven updates prevent visual artifacts

### Configuration System
- **Lesson**: Schema-driven configuration simplifies maintenance
  - Single field table drives NVS, web form, and serial console
  - Field descriptors enable automatic form generation
  - Template resolution order prevents misconfiguration

- **Lesson**: Clear separation of live vs reboot semantics is essential
  - Live fields apply instantly without requiring reboot
  - Reboot fields require restart to take effect
  - Documentation must clearly indicate which fields require reboot

- **Pitfall**: Missing validation can lead to hardware damage
  - Pin configuration must be validated against hardware constraints
  - Range clamping prevents invalid hardware states
  - Configuration changes should be atomic to prevent partial updates

### Web Interface
- **Lesson**: WebSocket provides efficient real-time status updates
  - Binary frames reduce bandwidth compared to JSON
  - Text frames enable command/response interaction
  - Connection state management ensures robust operation

- **Lesson**: Template-based HTML assembly enables maintainable frontend
  - Shared layout components reduce duplication
  - Firmware version injection ensures cache consistency
  - Static asset caching improves performance

- **Pitfall**: Large HTML templates can cause heap fragmentation
  - String concatenation should be managed carefully
  - Pre-allocation of buffers prevents memory issues
  - Gzip compression reduces memory usage

### Testing
- **Lesson**: Hardware-in-the-loop tests catch integration issues early
  - Real hardware testing validates timing and peripheral interactions
  - Automated testing improves release confidence
  - Test coverage metrics guide development efforts

- **Lesson**: Mocking enables comprehensive unit testing
  - WiFi event mocking enables isolated testing
  - Hardware abstraction enables testing without physical devices
  - Configuration system can be tested with in-memory storage

- **Pitfall**: Untested error paths can cause system instability
  - Error conditions must be explicitly tested
  - Edge cases require dedicated test scenarios
  - Recovery mechanisms need validation
- **Lesson**: Native test harness parity and assertion correctness
  - `test_net_baseline.c` (16 tests) and `test_led_math.c` (12 tests) cover WiFi portal activation matrix (spec 33 §2), exponential backoff formula (spec 14), STA/SoftAP connect, net state machine, config persistence round-trip, and LED brightness scaling/clamping/pattern-to-color mapping (spec 36). Native test count rose from 4 / 6 executables, 69 / 76 total test cases.
  - The ASSERT macro decremented `tests_run` (in addition to `tests_passed`) on failure, silently hiding test failures from the pass/total counter. Removing that line revealed a latent bug: `logger_get_ring_buffer()` returned NULL for an empty ring buffer, surfacing as a crash in the dump-test secret-masking check. Fixed both.


### Phase 1 - Implementation Status and Decisions (2026-08-19, ESP-IDF 6.0.1)
- **Env vs plan**: Framework is **ESP-IDF v6.0.1** (plan section 3.x says v5.2). No Phase-1 breakage; recorded per section 7.
- **NVS namespace split**: WiFi creds persist in namespace "wifi_config" (wifi_config.c); config schema fields persist in "dmxgw" (config_engine.c). Kept split for Phase-1 isolation; unify to `dmxgw`/`a_*` scheme at Phase-2 config growth (plan section 4).
- **Captive DNS (section 1.3 gap-b)**: No stock ESP-IDF component -> custom lwIP UDP:53 DNS sinkhole in captive_portal.c (all A-queries -> AP IP 192.168.4.1).
- **Serial grammar (spec 43)**: `dump/get/set/save/reset/load/wifi/reboot/help/factory/list` done; `save` persists without auto-reboot.
- **wifi_ssid flag**: changed to `CFG_REBOOT` per spec 45 (was `CFG_LIVE`; deferred OQ now resolved).
- **Board tables**: canonical boards.c exists; config_engine board-template reconciled (hardware fields sourced from boards.c, section 5.2).
- **Test harness ASSERT macro bug**: The ASSERT macro decremented both `tests_passed` and `tests_run` on failure, silently hiding test failures from the pass/total counter. Fixed by removing the `tests_run--` line so failures are properly counted. Also fixed the `dump` command secret-masking test to properly verify field presence and masking of non-empty secrets.
- **webui routes**: `/wifi/scan` + JSON `POST /setup` vs plan section 5.7 `/setup/scan` + 302; frontend matches device (accepted deviation).
- **Test-harness restore**: native was red (0/4) after WiFi event-handler WIP. Fixes: shim `IP_EVENT_*` offset 100/101 (collision with WIFI_EVENT_*=0/1); added `ip_event_got_ip_t`/`IPSTR`/`IP2STR`, `ESP_ERROR_CHECK`, `wifi_event_cb_t` signature + `#include "wifi_events.h"`; relocated use-before-def statics and removed duplicate `test_shim_set_psram_enabled`; explicit `<stdbool.h>`/`<stdlib.h>` in lux_config sources + test_config_serial.c. Result: native 4/4; `pio run -e esp32s3_psram` still green (shim changes native-only; include additions safe on-device).

## Phase 2: Config Engine (Full Schema)

### Schema Refactoring
- **Lesson**: offsetof-based descriptor tables eliminate the fragility of void* value_ptr
   - The Phase 1 config_values_t used direct void* pointers into struct fields — fragile during struct growth
   - The Phase 2 CfgField uses size_t offset (via offsetof) — descriptor table is the single source of truth, struct layout changes don't break field access

### NVS Migration
- **Lesson**: One-shot key migration must be idempotent
   - Legacy keys o0_*->a_*, o1_*->b_*, apfb->fbmode are migrated once and erased
   - Running migrateNvsKeys() twice is a no-op (idempotent)

### Template Inheritance
- **Lesson**: Template text parser with extends= enables recursive inheritance
   - Board templates use extends=_base to inherit global defaults
   - Inheritance capped at 8 levels per spec 45 §10

### Serial Grammar
- **Lesson**: save [reboot] provides a two-step persist+restart workflow
   - Plain `save` persists to NVS without rebooting
   - `save reboot` persists then sets the reboot flag for the main loop to act on

- **Pitfall**: Field name reconciliation between templates and C schema
   - Templates use compact keys (wifissid, wifipsk, ledpin) matching the spec
   - Phase 1 used verbose keys (wifi_ssid, led_pin) — Phase 2 reconciled to template keys

## Phase 3: DMX Output Core

### Seqlock Primitive
- **Lesson**: Sequence counter enables lock-free cross-core reads
   - Writer increments seq before/after publishing a new DMX slot buffer
   - Reader loops until seq is even and unchanged across the read — detects torn writes without mutexes
   - Used for the producer/consumer swap between ArtNet/sACN core-0 task and DMX output core-1 task
- **Pitfall**: Memory ordering must be enforced
   - `__sync_synchronize()` (or volatile + DMB) required before seq increment to prevent reordering
   - Compiler must not hoist reads out of the retry loop

### RMT Hardware TX
- **Lesson**: RMT channel provides precise DMX break + MAB timing
   - 88 µs break + 8 µs MAB generated via RMT item duration encoding (spec 45 §8)
   - Inter-slot gap (0 µs) and stop bit timing are exact — no bit-banging drift
- **Pitfall**: RMT TX done callback must not block
   - TX-end ISR queues the next slot for the next DMX packet; blocking stalls the stream
   - Buffer ownership (double-buffer / ping-pong) prevents mid-transit reconfiguration

### Crash Guard
- **Lesson**: Progressive output-disable heuristic with NVS-backed counter prevents boot-loop storms
   - `dmxInitGuardBegin()` reads the `dmxcrash` counter (uint8) from the `dmxgw` NVS namespace; if >0, it disables outputs [0..counter-1] in RAM so only the healthy tail stays active - incremental recovery instead of a hard shutdown.
   - `dmxInitGuardEnd()` writes counter+1, enters a 3000 ms stability window (10 ms vTaskDelay steps), re-reads, and resets to 0 on a stable boot; on mismatch or NVS failure it leaves the counter elevated (conservative: outputs stay disabled until a verified clean boot).

### sACN Stream-Sync
- **Lesson**: Stream-Sync staging decouples DMX flush timing from packet receipt
   - When `cfg.outputs[i].sacnsync > 0`, streaming data packets (frame vector 0x02) are staged into `g_dmxBufState.sacnStaged[i]` rather than routed immediately, with a 500 ms deadline (`sacnSyncDeadlineMs[i] = now_ms() + 500`) and sync universe address recorded in `sacnSyncAddr[i]`
   - Stream Sync packets (frame vector 0x03) commit staged frames only for outputs whose `sacnsync` matches the sync universe — a bug originally routed these through `flushArtSyncStaged()` (ArtNet path) instead of the sACN-specific `commitSacnStaged(i)`
   - `sacn_check_timeouts()` runs once per poll cycle after socket drain, committing frames whose 500 ms grace period has elapsed — this guarantees delivery even when the source never sends a Sync packet
   - `commitSacnStaged()` writes through the seqlock bracket (write-begin, slot copy with start code cleared to 0, write-end), zero-padding to DMX_PACKET_SIZE (513 bytes per spec 45 §11)
- **Pitfall**: Cross-protocol staging buffers must not share commit paths
   - ArtNet’s `flushArtSyncStaged()` touches `sacnSyncDeadlineMs[i]`, coupling the two protocols’ commit lifecycle — sACN Stream-Sync now commits exclusively via `commitSacnStaged(i)`, isolating protocol state
- **Pitfall**: Timeout comparison must use signed arithmetic to avoid wraparound
   - `uint32_t now - uint32_t deadline` wraps for long-running devices; `(int32_t)(now - deadline) >= 0` handles the 49.7-day millis() wrap safely

## Build System

### ESP-IDF Integration
- **Lesson**: Multi-board Kconfig simplifies hardware support
  - Board-specific configurations can be selected at build time
  - Configuration dependencies prevent invalid combinations
  - Default values ensure consistent builds

- **Lesson**: Template generation ensures consistent defaults
  - Build-time generation prevents runtime configuration errors
  - Configuration templates provide board-specific defaults
  - Generated headers reduce code duplication

- **Pitfall**: Incorrect Kconfig defaults can cause build failures
  - Default values must be validated for each target
  - Configuration options should have sensible defaults
  - Build flags must be compatible across all targets

### PlatformIO Integration
- **Lesson**: PlatformIO simplifies multi-board development
  - Environment definitions enable easy target switching
  - Build flags can be customized per target
  - Development tooling integration improves productivity

- **Pitfall**: PlatformIO-ESP-IDF integration has limitations
  - Some ESP-IDF features require direct configuration
  - Build system hooks may need custom implementation
  - Debugging support varies between targets

## General Lessons

### Migration Process
- **Lesson**: Incremental migration reduces risk
  - Small, focused phases enable early validation
  - Functional parity requirements prevent feature regression
  - Living documentation captures evolving decisions

- **Lesson**: Living documentation captures tribal knowledge
  - Architectural decisions are documented as they're made
  - Lessons learned are recorded throughout the process
  - Pitfalls are documented to prevent recurrence

- **Lesson**: Automated testing enables confident refactoring
  - Unit tests validate component behavior
  - Integration tests verify system interactions
  - Hardware tests validate real-world operation

### Embedded Development
- **Lesson**: Memory constraints require careful management
  - Heap fragmentation can cause system instability
  - Stack sizes must be carefully calculated
  - Memory allocation patterns affect long-term reliability

- **Lesson**: Timing constraints are critical for real-time systems
  - Core affinity prevents task preemption
  - Priority assignment ensures critical tasks execute on time
  - Interrupt handling must be optimized for performance

- **Pitfall**: Hardware-specific quirks can cause unexpected behavior
  - Different ESP32 variants have unique characteristics
  - Peripheral behavior may vary between chip versions
  - Silicon errata must be accounted for in driver design

## Future Considerations

### Phase 2: Core DMX Functionality
- **RMT Peripheral**: Hardware-accelerated DMX transmission
- **Seqlock**: Cross-core DMX buffer synchronization
- **Merge Engine**: HTP/LTP and priority merge algorithms
- **Protocol Parsers**: Art-Net and sACN packet decoding

### Phase 3: RDM Support
- **RDM Engine**: RMT-TX + UART-RX transport
- **RDM Discovery**: DISC_UNIQUE_BRANCH binary search
- **RDM Task**: Core-1 task for time-critical operations
- **Web RDM Interface**: Fixture management and sensor monitoring

### Phase 4: Advanced Features
- **Scene Engine**: Preset storage and fade engine
- **OTA System**: GitHub release integration and signature verification
- **Ethernet Drivers**: W5500 SPI and RMII LAN8720
- **Multi-Output**: 4-universe support with isolation

## Recommendations

1. **Maintain documentation discipline**
   - Update living documents throughout development
   - Capture architectural decisions as they're made
   - Document lessons learned from each phase

2. **Prioritize testing infrastructure**
   - Implement unit tests for all components
   - Develop integration tests for critical paths
   - Create hardware-in-the-loop test framework

3. **Focus on hardware abstraction**
   - Isolate board-specific configurations
   - Create clear interfaces for peripherals
   - Document hardware constraints and limitations

4. **Optimize for memory efficiency**
   - Monitor heap usage and fragmentation
   - Calculate stack sizes carefully
   - Use static allocation where possible

5. **Validate timing constraints**
   - Measure task execution times
   - Verify core affinity strategy
   - Test under worst-case network conditions



## Phase 5–7: RDM, Web Contract, OTA and System Services (2026-08-21)

### RDM transport contracts
- **Lesson**: E1.20 wire contracts should be isolated from hardware ownership. `rdm_types.h`, `rdm_engine.c`, and `rdm_discovery.c` are pure data/algorithm layers and can be validated on the host before RMT/UART integration.
- **Lesson**: A bounded UID-stack search is safer than recursive discovery. The implementation caps the search at `8 * max_devices + 128` probes and 64 range entries, deduplicates results, and requires a successful mute callback before publishing a UID.
- **Decision**: `rdm_task.c` owns a 32-entry core-1 queue with deep-copied request parameters. The current dispatcher builds and validates requests; physical RMT/UART transaction ownership remains with the driver integration phase.

### Web and REST
- **Lesson**: The fixed 2095-byte WebSocket frame is best implemented as a standalone serializer. `ws_frame.c` owns all big-endian offsets, the changed-universe bitmap, and the 12-client delta predicate, with native byte-conformance tests.
- **Decision**: REST JSON responses set `Cache-Control: no-store`. `/health`, `/dmx.json`, `/version.json`, `/setup/scan`, and `POST /reboot` were added without changing the existing provisioning routes.
- **Constraint**: ESP-IDF v6 currently generates `CONFIG_HTTPD_WS_SUPPORT=n` in the PlatformIO defaults. `web_websocket.c` therefore keeps a compile-time real-WS path and returns HTTP 426 from the safe fallback path rather than pretending that a manual HTTP upgrade is a WebSocket implementation. Enabling the Kconfig option is a release-build requirement.

### Build and security
- **Lesson**: The repository contained a cJSON gitlink without `.gitmodules`; cloning a fork therefore produced an unusable dependency. It was replaced with a vendored MIT-licensed cJSON component and an ESP-IDF CMake registration.
- **Decision**: OTA verification uses PSA PureEdDSA when `OTA_SIGN_ENABLED` or `CONFIG_LUXDMX_OTA_SIGN_ENABLED` is enabled. Development builds intentionally bypass verification; production profiles must enable the flag and provide the corresponding public key/streaming integration.
- **Lesson**: The native runner now has 14 green tests, while firmware builds must be cleanly reconfigured when component CMake source lists change; an incremental `pio run` can retain a stale component graph.

### Remaining hardware-gated work
- **Parity note**: RDM physical transaction orchestration, Ethernet PHY bring-up, OTA partition streaming/rollback, display/panel rendering, alert delivery, and soak telemetry require their respective hardware or platform integration. These are not silently represented as complete by the current host tests; the parity register and Phase 8 release gate must retain them as hardware-gated items.


## Issue #1: Secure OTA signing and boot-retry recovery (2026-08-22)

### Decisions
- **Signature-first commitment:** The OTA upload path calls `esp_ota_end()`, hashes the staged partition in 1 KiB reads, and verifies `Ed25519(SHA256(image))` over the trailing 64-byte signature before calling `esp_ota_set_boot_partition()`. Verification failure explicitly restores the running partition.
- **Bootloader-assisted recovery:** `otaRecoveryInit()` runs immediately after NVS initialization, reads `ESP_OTA_IMG_PENDING_VERIFY`, and persists the authoritative `dmxgw/boottry` counter. Three pending boots are allowed; a subsequent pending boot requests `esp_ota_mark_app_invalid_rollback_and_reboot()`. The app calls `otaRecoveryMarkHealthy()` only after the web, network, DMX, and RDM service graph has started.
- **Profile separation:** Development profiles bypass signing intentionally. The three `*_release` profiles define `OTA_SIGN_ENABLED=1`; tag CI materializes the private key only in the runner, checks it against the embedded public header, signs the firmware, and fails closed if the `LUXDMX_OTA_PRIVATE_KEY` secret is absent.

### Pitfalls
- **Generated SDK configuration is not a release contract:** `sdkconfig.*` is ignored and may preserve stale local values. The rollback requirement is therefore tracked in `sdkconfig.defaults`, with a targeted `.gitignore` exception, so clean CI checkouts receive the same Kconfig input.
- **CMake source lists can be stale:** After adding a component source, an incremental PlatformIO build may link an old component archive. A clean environment build is required when CMake source registration changes.
- **Production signing is not complete merely because verification compiles:** The checked-in public key is a placeholder. Hardware rollback and signed-image acceptance remain open until the maintainer provisions a real key pair and performs HIL tests without committing the private key.

### Validation
- `python3 tools/native_run.py --clean`: 15/15 CTest executables passed.
- `python3 tools/test_ota_sign.py`: host signed-image, tamper-rejection, and empty-image checks passed.
- Development and release profiles built successfully for `esp32dev`, `wt32eth01`, and `esp32s3_psram` after clean reconfiguration where required.


## Bước 2 — Canonical board capability and pin contract (2026-08-22)

The board table is now the single source for board identity, network capability, PSRAM/LED capability, and board-sensitive pin defaults. `board_config_t` exposes the same capability mask and pin map to runtime consumers while preserving the existing LED and network fields for compatibility.

A pin found only in a generic template is not sufficient evidence for a board-specific hardware claim. Pins that are undocumented or not yet HIL-verified are represented by `BOARD_PIN_UNASSIGNED` (`-1`), especially display, W5500 and multi-output DMX/RDM pins. The WT32-ETH01 RMII defaults are retained only where the existing board/spec contract already identifies MDC 23, MDIO 18 and power 16.

A capability bit means that a board profile can provision the interface; it does not mean that the runtime driver or physical board has passed HIL. Future Ethernet, RDM, display and panel work must consume this contract and add hardware evidence before changing the capability status.


## Bước 3.1 — Deterministic native test execution (2026-08-22)

Native CTest cases now carry the `native` label and a 10-second timeout. This makes a hung fixture or shim fail the CI job deterministically instead of consuming a worker indefinitely. The CTest property must reference the registered test name (`ws_frame_test`), not necessarily the executable target name (`ws_frame_test_runner`).


## Bước 3.2 — Fixture contract baseline (2026-08-22)

The native harness now has a small fixture contract that requires a non-empty fixture name, a bounded timeout from 1 ms through 10 minutes, and an artifact directory. A fixture can be marked `requires_hil` without making the native run claim hardware verification. This gives future protocol and HIL suites a common metadata shape while keeping the current native tests deterministic and hardware-independent.


## Bước 3.3 — ArtDMX fixture baseline (2026-08-22)

The test harness now includes a deterministic ArtDMX fixture builder that reuses the fixture contract and produces bounded packets with the documented Art-Net ID, little-endian opcode/universe, big-endian protocol version/length, sequence, priority, and 512-slot limit. Its tests cover valid payloads, zero-length payloads, endian/layout fields, and invalid bounds.

This fixture is intentionally host-side test data, not an implementation of `artnet_dispatch_packet()` and not proof of network interoperability. Packet exchange, routing, source tracking, and WiFi/Ethernet behavior remain later protocol E2E or HIL gates.


## Bước 3.4 — Art-Net dispatcher integration fixture (2026-08-22)

The native harness now compiles the real Art-Net dispatcher with POSIX-backed lwIP header shims and test-owned route/bridge captures. The fixture verifies ArtDMX routing, received-length clamping, ArtNzs start-code routing, ArtSync dispatch, control-plane bridge forwarding, and null/short/unknown packet rejection.

The test deliberately does not open a real UDP socket and does not claim WiFi/Ethernet interoperability. It is a dispatcher-level contract test; protocol exchange, routing over a live interface, and packet-capture evidence remain separate E2E/HIL gates.


## Bước 3.5 — Fixture artifact and cleanup reporting (2026-08-22)

The fixture harness now reports a linear lifecycle of created, setup, exercised, and cleaned. Invalid out-of-order transitions are rejected, cleanup is required for a complete report, and artifact counts are bounded at 1024. The report is metadata-only: it does not create or delete files, so CI wrappers and future HIL runners can own filesystem cleanup explicitly.


## M0.4.1 — Repository hygiene gate (2026-08-22)

The repository now has a dependency-free `tools/repository_hygiene.py` checker and `tools/test_repository_hygiene.py` violation suite. The checker distinguishes tracked source-of-truth from generated build output, generated sdkconfig files, firmware artifacts and private-key material; it also checks workspace outputs and `git diff --check` unless `--tracked-only` is selected.

CI validators must validate YAML syntax and required shape without hard-coding historical job names that may legitimately change during a planned CI migration. A stale local validator can fail for the wrong reason, so the validator itself must be reviewed or replaced before treating its result as evidence.


## M0.4.2 — CI log and failure artifacts (2026-08-22)

CI diagnostics now have a dependency-free allowlist collector in `tools/ci_capture_context.py` and a two-case host test. The collector records only revision, short status, safe tool versions, runner platform, job label and an explicit no-environment-dump policy. It never serializes process environment variables, signing keys or tokens.

Long-running CI commands must use `set -o pipefail` with `tee`, otherwise a successful tee can hide the real build/test exit code. Diagnostics should use separate `diagnostics-<job-or-environment>` namespaces and `if: always()` upload behavior, while the quality-gate command itself must remain fail-closed. The workflow changes for this step remain pending GitHub `workflows` permission; the collector and documentation are already validated locally.


## M0.4.3.1 — Firmware matrix and artifact contract (2026-08-22)

The six PlatformIO firmware environments are now documented as three development gates and three signing-enforced release gates. Native remains a separate test environment. Firmware inputs are isolated as `firmware-<env>` artifacts, while ELF/map/size evidence belongs to diagnostics artifacts.

Artifact names must use the exact PlatformIO environment name so development and release outputs cannot overwrite or be confused. The next sub-step must produce stable per-environment JSON metadata with byte sizes and SHA-256 values; a successful compile or local flash-size warning is not hardware validation.


## M0.4.3.2 — Bounded firmware size/metadata report (2026-08-22)

The repository now has `tools/firmware_artifact_report.py`, a dependency-light reporter for one PlatformIO environment at a time. It resolves `extends`, distinguishes development/release profile, records board/framework fields, computes byte sizes and SHA-256 values with bounded 1 MiB streaming reads, and rejects missing required artifacts or files over the 64 MiB safety bound.

A report with missing toolchain metadata may still be useful for a local invocation, but it must not be interpreted as hardware validation. CI integration and all-environment matrix execution remain separate work. Required firmware inputs remain fail-closed: missing `firmware.bin`, `partitions.bin` or `bootloader.bin` makes the report fail.


## M0.4.3.3 — Firmware matrix reporter integration (2026-08-22)

The development firmware matrix now runs `tools/firmware_artifact_report.py` after each successful build and uploads `firmware-metadata-<env>` separately from `firmware-<env>` binaries and `diagnostics-firmware-<env>` logs. The report step is fail-closed: a missing required binary or unsafe file prevents metadata upload and keeps the build job failed.

The current CI matrix still contains the three development environments only. The three release profiles remain a separate release-matrix gap and must not be inferred as covered from development artifacts.


## M0.4.3.4 — Matrix validation and closeout (2026-08-22)

The bounded firmware reporter was exercised after a successful `pio run -e <env>` for all six PlatformIO profiles: `esp32dev`, `wt32eth01`, `esp32s3_psram`, `esp32dev_release`, `wt32eth01_release`, and `esp32s3_psram_release`. Every report returned `status: pass`; required `firmware.bin`, `partitions.bin`, and `bootloader.bin` were present and hashed. The development profiles remain the deployed PR/`main` CI matrix, while release profiles are validated locally here and remain governed by tag/release workflow policy.

The report schema is nested under `artifacts`, so consumers must read `bytes` and `sha256` from each artifact record rather than expecting legacy top-level size fields. The report records the exact source commit and stable UTF-8 JSON, making it suitable for later packaging and hosted-flasher consumption.

The ESP32-S3 PlatformIO invocation emitted a local flash-size mismatch warning even though the configured profile expects 8 MB and the build completed successfully. This is build-tool evidence only; it does not establish flash geometry correctness, PSRAM behavior, signing-key provisioning, OTA acceptance, PHY behavior or HIL readiness. Those claims remain explicitly outside M0.4.3.


## M0.4.4.1 — Generated-assets inventory (2026-08-22)

The repository specifications describe a template generator and PROGMEM asset generator, but the current checkout has no active PlatformIO `extra_scripts`, custom CMake generation command or generated include path. The config runtime currently consumes inline board-template strings in `components/lux_config/src/config_schema.c`, while the web runtime consumes inline HTML/CSS/JavaScript strings in `components/lux_web/src/web_frontend.c` and `components/lux_web/src/web_assets.c`.

The tracked `templates/` and `webui/` trees are therefore candidate inputs, not proven build inputs. M0.4.4 must not generate unused headers or connect a hook based only on documentation claims. Inventory and source-of-truth reconciliation must precede generator implementation; any asset without a verified consumer remains `documented-but-missing` or `blocked`, not `done`.


## M0.4.4.2 — Pure generator CLI and manifest schema (2026-08-22)

`tools/generate_manifest.py` now provides a dependency-free host-side materializer for an explicit input set. It validates relative POSIX paths, rejects symlinks and non-canonical components, applies 64 MiB per-file and 256 MiB total bounds, copies/hashes in 1 MiB chunks, and publishes only after a complete staging directory and UTF-8 manifest are ready.

The `luxdmx.generated-manifest.v1` schema deliberately records only stable generator identity, sorted input/output relative paths, byte counts, SHA-256 values, status and deterministic errors. It excludes commit, timestamp, host, absolute path, process environment and secrets. The generic layer copies bytes without imposing text semantics; UTF-8 and `extends=` validation remain template-generator responsibilities, while web asset encoding remains conditional on a verified runtime consumer.

The order-independent double-run smoke test produced byte-identical output trees and manifests. An existing output directory is refused rather than merged, preventing stale generated files from being mistaken for current output. No PlatformIO/CMake hook or runtime consumer was changed in this package.


## M0.4.4.3 — Deterministic template inheritance (2026-08-22)

`tools/template_generator.py` now resolves explicit UTF-8 `.ini` roots and their transitive `extends=` parents. Resolution is base-first: parent values retain order, child overrides replace values in place, and new child keys append deterministically. Same-file duplicate keys and duplicate `extends` directives are rejected; child overrides of parent keys are intentional and tested.

The resolver rejects missing parents, cycles, excessive depth, invalid UTF-8, malformed lines, empty keys, non-canonical paths, symlinks and unsafe input/output sizes. It emits normalized `key=value` outputs plus a `templates` inheritance record in the stable manifest. Five repository templates were resolved twice with different root ordering and produced byte-identical output trees.

This package remains host-only. The current firmware still consumes inline template strings in `config_schema.c`; no PlatformIO/CMake hook or runtime source-of-truth migration was performed. That integration remains a separate gate and must not be inferred from the passing parser tests.


## M0.4.4.4 — Web/static asset evidence review (2026-08-22)

The tracked `webui/` tree is not currently proven to be the firmware asset source-of-truth. It contains a setup portal (`LuxDMXApp`) with `css/style.css`, `js/transport.js` and `js/app.js`, while the firmware serves dashboard/config/setup/firmware pages from inline C strings and exposes only `/assets/style.css` and `/assets/app.js` through `web_assets_get()`.

A byte-level comparison found no exact match for the root page, CSS or JavaScript. The runtime root page is 4,827 bytes versus 3,232 bytes for `webui/index.html`; runtime CSS is 3,666 bytes versus 6,145 bytes; runtime JavaScript is 1,641 bytes versus 7,928 bytes. The runtime also has no tracked counterparts for config/setup/firmware pages, and no confirmed route for `transport.js`.

M0.4.4.4 is therefore deliberately marked `Blocked / documented-only`. Generating or embedding `webui/` now would risk replacing dashboard behavior with setup behavior and would create output without a verified consumer contract. A future frontend/runtime reconciliation package must choose the canonical source, map every route, prove byte/content intent, add consumer tests and define encoding/compression policy before asset generation.


## M0.4.5.1 — Web boundary freeze (2026-08-24)

The web baseline is broader than the HTML/CSS/JavaScript files under `webui/`. The active HTTP server registers 14 route entries from `web_routes.c`, including HTML, JSON, POST and wildcard asset routes; `otaManagerRegister()` adds `/ota/status` and `/ota/upload`; `web_websocket_init()` adds `/ws`. The live static allowlist still contains only `/assets/style.css` and `/assets/app.js`, even though the route handler has MIME branches for `.ico` and `.png`.

The freeze records route methods, handler functions, response types, request limits, API fields, cache/reboot behavior, inline frontend consumers, tracked webui candidates, source fingerprints and build registration. It intentionally makes no canonical-source decision. The next safe step is a route/page/API/asset parity matrix before any frontend rewrite or asset-generator hook.
