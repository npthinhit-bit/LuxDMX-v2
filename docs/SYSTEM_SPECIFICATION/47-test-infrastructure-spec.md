# Test Infrastructure Specification

Domain: system.test

## 1. Module Overview

The Test Infrastructure provides two tiers of automated testing for the firmware:

1. **Native host tests** ï¿½ standalone programs compiled with GCC or MSVC against shim headers that emulate the Arduino and ESP-IDF APIs. No external test framework is required; each test uses a lightweight check macro and returns an exit code.
2. **Unity unit tests** ï¿½ PlatformIO-managed tests compiled with the Unity framework, using assertion macros with setup/teardown fixtures. Tests run against the native (host) environment with the same shim headers.

The native tests verify the config engine, the seqlock concurrency primitive, the merge engine, and RDM type constants without hardware. The Unity tests provide equivalent coverage with the Unity framework. A third tier of end-to-end tests (Playwright) drives a live device over the network for integration validation.

The infrastructure owns the test runner scripts, the shim headers that emulate Arduino String, Preferences (NVS), millis, and synchronization primitives, and the test entry points. It delegates compilation to GCC for native tests and to PlatformIO for Unity tests. It is consumed by all firmware modules under test (config, core, driver layers) and by developers running the test suites.

## 2. External Interfaces

### Native Host Tests

| Test | Scope | Assertions |
|---|---|---|
| config_test | Config engine: template resolution, single-field set/get, NVS save/load round-trip, serial console grammar, secret masking | All verbs: dump, get, set, key=value, help |
| seqlock_test | Generic seqlock: clean snapshot, stable write copy, 100 write-read cycles | Snapshot integrity under concurrent write |
| merge_test | Merge engine: HTP, OFF/LTP, LOSS_ZERO, LTP-Takeover priority, priority merge, cross-universe isolation, per-port failsafe timeouts | Merged output values per mode |
| rdm_types_test | RDM types: UID equality, broadcast/max UID, response types, PID constants, command constants, packet size, ASCII size, sensor types, sensor units, UID pointer comparison | Type constants and pack/unpack behavior |

Each native test is a standalone test entry point that returns exit code 0 on success, 1 on failure. Tests print a count of passed/failed assertions.

### Unity Unit Tests

| Test | Scope | Framework |
|---|---|---|
| test_config | Config defaults, set/get, NVS round-trip, board template differences, all serial commands including invalid input | Unity assertion macros |
| test_merge | Merge engine coverage | Unity assertion macros |
| test_rdm_types | RDM type constants and structs | Unity assertion macros |
| test_seqlock | Generic seqlock primitive | Unity assertion macros |

Unity tests use the standard setup/teardown fixture pattern with assertion macros registered in the test entry point.

### End-to-End Tests

| Suite | Scope | Requirement |
|---|---|---|
| Playwright E2E | Web interface, REST API, WebSocket binary streams, Art-Net/sACN packet exchange, OTA flow | Requires a live device on the network |

### Test Shims

The native and Unity environments use shim headers that emulate platform-specific APIs on the host:

| Shim | Emulates |
|---|---|
| Arduino core | String class (backed by std::string), millis() (incrementing counter), constrain(), no-op synchronization primitive, compiler attributes |
| Non-volatile storage | In-memory key-value store backed by std::map, mimicking the flash-based NVS API |
| Error constants | Success and error code constants |
| Logging | Logging stub (no-op or minimal output) |
| Heap allocation | Heap allocation stub (returns success) |
| RMT driver | RMT bus types and stubs |
| UART driver | UART port stubs |

## 3. State Machine

No state machine. Both test tiers are linear: setup to exercise to assert to teardown. Each test function runs independently with a clean fixture. The native tests have no inter-test state; the Unity tests use setup/teardown fixtures to reset the NVS shim and configuration to a known template state before each test function.

## 4. Data Flow

### Native Host Test Flow

1. **Test runner**: A wrapper script selects a test name and invokes the runner.
2. **Compilation**: GCC compiles the test source alongside a selective subset of firmware sources, linking against the shim headers. Selected firmware sources include: config engine, schema, merge engine, DMX buffer, sender tracking, and statistics.
3. **Execution**: The test binary entry point, which calls setup-like initialization (e.g., reset configuration to template defaults, clear the NVS shim), exercises the target module, asserts via the check macro, and prints pass/fail counts.
4. **Exit**: The binary returns exit code 0 (all pass) or 1 (one or more failures), which the runner reports.

### Unity Test Flow

1. **Build**: PlatformIO compiles the test sources and a selective subset of firmware sources (determined by the build source filter) against the shim headers.
2. **Setup fixture**: Before each test function, the setup fixture clears the NVS shim and resets configuration to template defaults.
3. **Execution**: The test runner invokes each test function, which exercises the target and asserts via assertion macros.
4. **Unity framework**: The Unity framework handles pass/fail accounting and detailed failure reporting.
5. **Exit**: Unity reports the test summary and returns an exit code.

### Cross-Tier Relationship

The native and Unity tests cover the same core modules (config, merge, seqlock, RDM types). The native tests serve as fast smoke tests requiring no PlatformIO; the Unity tests run within the PlatformIO ecosystem with richer assertion reporting.

## 5. Configuration Integration

### Native Test Configuration

| Aspect | Setting |
|---|---|
| Default template | The base board template provides defaults (hostname, protocol, output A/B) |
| 4-universe template | A second template provides defaults for 4-output boards (RDM-full vs DMX-only outputs) |
| NVS round-trip | The NVS shim provides an in-memory store; tests save, reset, then load to verify persistence |
| Serial console | Tests invoke the console command executor directly to verify dump, get, set, and invalid commands |

### Unity Test Configuration

| Aspect | Setting |
|---|---|
| Default template | Same base and 4-universe templates |
| NVS round-trip | NVS shim cleared in setup fixture before each test |
| Build source filter | Selects only specific firmware source files (config, merge, seqlock, RDM types) for compilation |

## 6. Lifecycle

1. **Pre-test**: The build system generates the template header (board config templates embedded as constants) needed by the config engine source.
2. **Compile**: GCC (native) or PlatformIO (Unity) compiles test sources and selected firmware sources against shims.
3. **Run**: The test binary executes setup, exercises the module, asserts, and reports.
4. **No deinit**: Tests exit after the entry point returns; no teardown phase.

## 7. Error Handling

| Condition | Handling |
|---|---|
| Test assertion failure | Native: increments fail counter, prints FAIL with message, returns exit code 1. Unity: The assertion macro fails, Unity reports the failure with file/line context |
| Unknown config key | setValue returns an invalid-argument error code; test asserts the error |
| NVS key missing | NVS shim returns the default value (0/false/empty) |
| Invalid serial command | Console executor returns an error string; test verifies the error |
| Build source filter excludes a module | If a test references a symbol not in the filtered sources, the link fails |

## 8. Timing Constraints

Tests are deterministic and single-threaded. The millis() shim returns incrementing integers starting from zero, so timing-dependent assertions are reproducible across runs. No real-time constraints. The native tests compile and run in a few seconds; the Unity tests take longer due to PlatformIO's build overhead.

## 9. Memory and Allocation Model

- **Native tests**: Stack-allocated data structures, static pass/fail counters, std::map backing the NVS shim on the host heap.
- **Unity tests**: Stack-allocated test data, NVS shim backing store on the host heap.
- **Preferences shim**: Backed by a std::map on the host; no real NVS or flash access occurs.
- **No hardware memory allocation or PSRAM** in test mode; the heap allocation shim stubs return success for all allocations.

## 10. Safety Considerations

- **Test isolation**: Each test resets to a known state (template defaults + cleared NVS shim) before execution, preventing cross-test contamination.
- **No hardware dependency**: Tests run entirely on the host, enabling continuous integration without physical devices.
- **No real NVS access**: The NVS shim emulates storage in memory, so tests never touch actual flash and cannot corrupt device state.
- **Selective source inclusion**: The Unity build source filter includes only specific firmware modules, preventing tests from accidentally pulling in hardware-dependent code (Ethernet, WiFi, RMT drivers) that cannot compile on the host.

## 11. Cross-Module Dependencies

| Module | Provides to Tests | Consumes from Tests |
|---|---|---|
| Config Engine | Configuration descriptors, template defaults, NVS API | - (tests exercise the API) |
| Seqlock | Lock-free buffer primitive | - |
| Merge Engine | DMX source merging logic | - |
| RDM Types | E1.20 type definitions and pack/unpack | - |
| Native platform shims | Emulated platform APIs | All native/Unity test sources |
| PlatformIO | Build orchestration for Unity tests | Unity framework library |
| Playwright | Browser automation for E2E | Live device |

## 12. Testing Verification

| Test Suite | Framework | Coverage |
|---|---|---|
| config_test | Native (check macro) | 5 tests: template resolution, 4-universe template, set/get, NVS round-trip, serial console |
| seqlock_test | Native (check macro) | 3 tests: clean snapshot, stable write copy, 100 write-read cycles |
| merge_test | Native (check macro) | 8 tests: HTP, OFF, LOSS_ZERO, priority takeover, priority merge, cross-universe isolation, failsafe timeouts, preset/home fallback |
| rdm_types_test | Native (check macro) | 10 tests: UID equality, broadcast/max UID, response types, PID constants, command constants, packet size, ASCII size, sensor types, sensor units, UID pointer comparison |
| test_config | Unity | 8 tests: defaults, set/get, NVS round-trip, 4-universe template, serial set, serial get, serial dump, invalid input |
| test_merge | Unity | Merge engine (Unity equivalent of native merge_test) |
| test_rdm_types | Unity | RDM type constants and structs (Unity equivalent) |
| test_seqlock | Unity | Generic seqlock (Unity equivalent) |

## 13. Open Questions

1. The exact location of the native test runner script ï¿½ the wrapper scripts reference a script that is not present in the repository.
2. Whether the Unity test_seqlock tests the generic seqlock primitive or the DMX buffer wrapper.
3. Whether the Unity test_merge mirrors the native merge_test cases exactly.
4. Whether additional Playwright E2E test scenarios (beyond the documented web UI, REST API, WebSocket, and OTA) are planned.

## 14. History

- The two-tier test infrastructure was established: native host tests for fast smoke testing without PlatformIO, and Unity unit tests for the PlatformIO ecosystem with richer assertion reporting.
- The Unity [env:unit-test] environment was configured with a selective build source filter to include only hardware-independent modules (config, merge, seqlock, RDM types), preventing link failures from hardware-dependent code.
- The Preferences shim was implemented as an in-memory std::map to emulate the ESP-IDF NVS API on the host, enabling config save/load round-trip tests without flash.
- The millis() shim was implemented as an incrementing counter starting from zero, ensuring deterministic timing in tests.
- The synchronization primitive shim was implemented as a compiler memory barrier, providing correct seqlock semantics in single-threaded host tests.
- Playwright end-to-end tests were added as a third tier for integration validation against live hardware, covering the web interface, REST API, WebSocket binary streams, protocol exchange, and OTA flow.
