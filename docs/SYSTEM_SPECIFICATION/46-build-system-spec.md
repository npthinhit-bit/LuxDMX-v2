# Build System Specification

Domain: system.build

## 1. Module Overview

The Build System is a PlatformIO-based build orchestration layer that compiles the firmware for three supported board targets, three explicit signed-release profiles, and one native test environment. It is managed through a single build manifest that defines compiler flags, board configurations, dependency libraries, partition layouts, and build-time code generation for embedded web assets and board configuration templates.

The system owns the build manifest configuration (environments, flags, dependencies), the toolchain path resolution hook, the PROGMEM header generator (which gzips HTML pages and binary assets into embedded arrays), and the board template generator (which embeds per-board default configuration values as read-only strings). It delegates compilation to the ESP-IDF CMake builder and code generation to a Python script invoked as a build hook.

It is consumed by all firmware source files (via include paths), the config engine (which reads the embedded template table at boot), and the test infrastructure (which uses a native build environment).
## 2. External Interfaces

### Build Environments

The build manifest defines three development firmware environments, three signed-release variants, and one native test environment.

| Environment | Target | MCU | Network | Outputs | Key Defines |
|---|---|---|---|---|---|
| esp32dev | ESP32 Dev Module | ESP32 | WiFi | 2 | Development profile; signature verification bypassed |
| esp32dev_release | ESP32 Dev Module | ESP32 | WiFi | 2 | Release profile; `OTA_SIGN_ENABLED=1` |
| wt32eth01 | WT32-ETH01 | ESP32 | RMII LAN8720 Ethernet | 2 | Development profile; signature verification bypassed |
| wt32eth01_release | WT32-ETH01 | ESP32 | RMII LAN8720 Ethernet | 2 | Release profile; `OTA_SIGN_ENABLED=1` |
| esp32s3_psram | ESP32-S3 DevKitC-1 + PSRAM | ESP32-S3 | WiFi | 2 | Development profile; octal PSRAM; signature bypassed |
| esp32s3_psram_release | ESP32-S3 DevKitC-1 + PSRAM | ESP32-S3 | WiFi | 2 | Release profile; octal PSRAM; `OTA_SIGN_ENABLED=1` |
| native | Host (GCC/MSVC) | x86 | None | None | Test-only environment with shims |

### Build Flags (shared)

All firmware environments inherit a set of shared build flags:

| Flag | Value | Purpose |
|---|---|---|
| AsyncTCP stack size | 16384 (16 KB) | Asynchronous TCP task stack |
| AsyncTCP queue depth | 128 | AsyncTCP pending operation queue |
| AsyncTCP running core | 0 | Pin AsyncTCP to core 0 to isolate RDM on core 1 |
| AsyncTCP priority | 10 | AsyncTCP task priority |
| Max outputs | 4 | Maximum DMX outputs supported |
| Max senders | 16 | Maximum active DMX sources tracked |
| RDM tod max | 64 | RDM Table of Devices entry limit |
| Log buffer cap | 32 | Syslog/log buffer capacity |

Include path flags expose the public headers for all firmware layers to each compilation unit.

### Environment-Specific Defines

| Environment | Defines |
|---|---|
| All | DEFAULT_TEMPLATE board name, signature verification disabled |
| esp32dev | HAS_WIRED_ETH, HAS_ETH_SPI |
| wt32eth01 | HAS_WIRED_ETH, HAS_ETH_RMII |
| esp32s3_n16r8_eth | BOARD_4_UNIVERSE, SOAK_TEST_MODE, W5500 Ethernet enabled |

## 3. State Machine

The build is a linear pipeline with no state machine:

1. Pre-build: toolchain path resolution and header generation
2. Compile: all sources for the selected environment
3. Link: produce the firmware binary
4. Optional: flash to device or run tests

## 4. Data Flow

### Build Pipeline

1. **Toolchain resolution**: The build hook locates the ESP32 toolchain (xtensa-esp-elf-gcc 14.2.0) in the PlatformIO packages directory and prepends its binary path to the system PATH, resolving nested toolchain directory structures.
2. **Source compilation**: PlatformIO compiles all C++ source files under the source tree according to the include path flags for the selected environment.
3. **PROGMEM header generation**: A build hook gzips HTML page files into embedded byte arrays and encodes binary assets as PROGMEM arrays, producing generated header files consumed by the web server and asset serving code.
4. **Template generation**: A Python script reads all board template files, strips comments and blank lines, sorts them with the base template first, and emits a generated header containing each template as a read-only constant string plus a registry table. A wrapper source file includes this header so it compiles into the firmware.
5. **Compile**: The resolved toolchain compiles all firmware sources with the environment-specific flags.
6. **Link**: The toolchain linker produces an ELF binary, then converts to a flashable BIN via the ESP flash tool.
7. **Test (native)**: For the native test environment, the build system compiles test source files alongside selected firmware sources against host shim headers, producing standalone test binaries.

### Template Resolution Flow

1. The base template provides global defaults (hostname, LED, network, output A/B defaults).
2. A board-specific template extends the base template via an inheritance directive, overlaying board-specific pin assignments and defaults.
3. The template generator sorts templates with the base first, then emits each as a constant string.
4. It builds a registry table mapping template names to their content, with a template count.
5. At boot, the config engine resolves the active template (selected at build time), applies neutral constraint values, then applies the template, then overlays saved non-volatile storage values.

## 5. Configuration Integration

Build-time configuration is controlled through the build manifest:

| Setting | Source | Description |
|---|---|---|
| Active board template | Per-environment define | Selects the base configuration template (e.g., esp32dev, esp32s3dev, esp32s3_psram, 4-universe board) |
| Wired Ethernet enable | Per-environment define | Compiles wired Ethernet code paths |
| SPI Ethernet driver | Per-environment define | W5500 SPI Ethernet driver |
| RMII Ethernet driver | Per-environment define (wt32eth01 only) | LAN8720 RMII Ethernet driver |
| Signature verification | Per-environment define | Ed25519 verification (disabled only in named dev profiles; enabled in `*_release`) |
| Soak test mode | 4-universe env define | 60-second heap watchdog and diagnostics endpoint |
| 4-universe board | 4-universe env define | 4-DMX-output pin map with copper pour isolation |

Board templates are data files (INI format) that provide default values for every configuration field. They are the neutral layer in the resolution order: neutral constraint value (compile-time) then board template (runtime from embedded constants) then saved NVS value (runtime from flash).

## 6. Lifecycle

1. **Pre-build**: The build hook runs before compilation: toolchain PATH fix and PROGMEM header generation (HTML gzip embedding and template generation).
2. **Build**: The developer invokes the build for a specific environment, compiling all sources with the environment flags.
3. **Flash** (optional): The compiled binary is uploaded to the target device via the selected upload protocol.
4. **Monitor** (optional): A serial terminal is opened to observe runtime logs.
5. **Test**: The native test runner or Unity test suite is executed to validate modules without hardware.

No explicit shutdown or cleanup phase exists in the build system.

## 7. Error Handling

| Condition | Handling |
|---|---|
| esp-modbus Kconfig conflict | The esp-modbus component is explicitly removed from the build - its default configuration conflicts with static assertions in the ESP-IDF core |
| S3 brownout before setup | The ESP32-S3 brownout detector is disabled via a custom SDK configuration override, preventing a boot-loop that occurs when the detector fires before the firmware initializes |
| Missing libatomic on S3 from-source builds | Avoided by not using C++ shared pointers or atomics in the firmware source - a code-level constraint documented in the build manifest |
| Toolchain PATH resolution failure | The build hook prepends the toolchain binary directory from multiple candidate locations (PlatformIO packages or local tools directory) |
| Include path collision on S3 builds | The source/system include flag is NOT added to from-source S3 builds, because it would shadow the ESP-MQTT module platform abstraction header |
| W5500 Ethernet not in build | The wired Ethernet code paths are gated behind a preprocessor define; without it, a warning is emitted at runtime |
## 8. Timing Constraints

No build-time timing constraints exist. Runtime timing is governed by FreeRTOS task periods:

| Task | Core | Period | Purpose |
|---|---|---|---|
| DMX transmit | Core 1 | 1 ms | Frame tick, RMT transmit, RDM service |
| Network receive | Core 0 | 2 ms | Art-Net and sACN packet drain |
| LED status | Either | 50 ms | Status indicator update |
| Display | Either | 200 ms | OLED or SPI display render |
| Firmware check | Either | 60 s | Background firmware version check |

The AsyncTCP library is pinned to core 0 to prevent it from preempting the time-critical RDM path on core 1. Measurements show that with AsyncTCP on core 1 at priority 10, the RDM transmit-to-receive turnaround inflates from 15 to 53 microseconds (median) with a 126 microsecond maximum.

## 9. Memory and Allocation Model

- **Build-time static PROGMEM**: Gzipped HTML pages and binary assets are embedded as read-only byte arrays in flash memory.
- **Build-time static templates**: Board configuration templates are embedded as read-only constant strings, residing in memory-mapped flash directly readable without PSRAM or heap allocation.
- **Runtime heap**: On ESP32-S3 PSRAM builds, WiFi and TCP/IP stack buffers are allocated in PSRAM via the SPIRAM configuration flags.
- **Partitions**: The flash partition layout uses a dual-slot OTA scheme with filesystem (SPIFFS) partition, defined by a partition table CSV file.
- **Stack allocation**: Task stack sizes are configured per-environment via build flags (AsyncTCP task uses 16 KB).

## 10. Safety Considerations

- **Core isolation**: The AsyncTCP and WiFi stack run on core 0, isolated from the time-critical DMX transmit and RDM service on core 1. This prevents network activity from preempting or corrupting DMX break and mark timing.
- **RMT-based DMX transmission**: DMX is clocked by the RMT peripheral in hardware, not by the CPU or UART. If the refill interrupt is ever delayed, the RMT idles the line (a benign extra mark) rather than corrupting a break, eliminating the core-0 network DMA contention bug that previously corrupted breaks under heavy WiFi load.
- **ESP32-S3 brownout prevention**: The brownout detector is disabled via a custom SDK configuration override, preventing a boot-loop that occurs when the detector fires before the firmware's setup() function initializes. This is critical for from-source ESP32-S3 builds.
- **Component conflict avoidance**: The esp-modbus component is explicitly removed because its Kconfig defaults conflict with static assertions in the ESP-IDF core, which would prevent compilation.
- **Include path isolation**: The from-source S3 builds deliberately omit a source directory include flag that would shadow the ESP-MQTT module platform abstraction header, preventing a build-time collision.

## 11. Cross-Module Dependencies

| Module | Relationship |
|---|---|
| PlatformIO | Consumes the build manifest to compile, link, and test all environments |
| ESP-IDF CMake builder | Consumed by PlatformIO for from-source ESP32-S3 builds |
| Toolchain (xtensa-esp-elf-gcc) | Consumed for compilation and linking of firmware |
| Template generator (Python) | Consumed by the build hook to embed board templates |
| PROGMEM header generator | Consumed by the build hook to embed web assets |
| Config Engine | Consumes the generated template header at boot for configuration initialization |
| All firmware layers | Consume the include path flags to locate headers |
| Test modules | Consume the native build environment and shims |

## 12. Testing Verification

| Test Suite | Framework | Scope |
|---|---|---|
| Native config test | Host (check macro) | Template resolution, set/get, NVS round-trip, serial console grammar |
| Native seqlock test | Host (check macro) | Seqlock snapshot under concurrent write |
| Native merge test | Host (check macro) | Merge engine: HTP, LTP, LOSS_ZERO, cross-universe, priority takeover, failsafe timeouts |
| Native RDM types test | Host (check macro) | UID pack/unpack, enums, PID constants |
| Unity config test | Unity | Config defaults, set/get, NVS round-trip, serial commands |
| Unity merge test | Unity | Merge engine coverage |
| Unity seqlock test | Unity | Generic seqlock primitive |
| Unity RDM types test | Unity | RDM type constants and structs |

The native tests compile selected firmware source files alongside test sources using host shim headers that provide Arduino and ESP-IDF API stubs. No external test framework is required for native tests.

## 13. Open Questions

1. Whether the build/test scripts referenced by the native test runners (run scripts that invoke the test runner) exist in the repository or are broken references.
2. The exact partition table layout values (defined in a file not directly inspected).
3. Whether from-source ESP32-S3 builds that use C++ shared pointers or atomics would resolve libatomic.a from an alternate source, or if those C++ features are permanently unavailable on this build path.

## 14. History

- AsyncTCP hardening block was added to the build flags, pinning AsyncTCP to core 0 with a 16 KB stack, queue depth 128, and priority 10, to isolate the RDM timing path on core 1.
- The ESP32-S3 brownout detector was disabled via a custom SDK configuration override to prevent a boot-loop that occurs when the detector fires before firmware setup.
- The esp-modbus component was explicitly removed to avoid a Kconfig conflict with ESP-IDF static assertions.
- The include path flags were moved from the global build flags to per-environment configuration for ESP32-S3 from-source builds, to prevent shadowing the ESP-MQTT module platform abstraction header.
- The 4-universe environment was added with PSRAM, W5500 Ethernet, and soak test monitoring for the 4-output board.
- Board template configuration defaults replaced scattered preprocessor macros, moving to data files embedded at build time.
- Template selection moved from a global preprocessor macro to a per-environment build flag.
