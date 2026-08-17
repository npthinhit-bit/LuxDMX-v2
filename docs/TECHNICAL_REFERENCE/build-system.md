Build System — Technical Reference

Domain: `system.build`

## 1. Domain Scope

The LuxDMX-v2 build system is PlatformIO-based with an Arduino-ESP32 v3 (IDF 5.x) core, managed via `platformio.ini`. It supports 5 firmware build environments (ESP32, ESP32-S3 DevKitC-1, WT32-ETH01, ESP32-S3+PSRAM, ESP32-S3 4-universe), a native host test environment, and two test runners (native smoke tests + Unity unit tests). Build-time code generation embeds gzipped HTML pages, binary assets, and board config templates into PROGMEM headers.

**Owns:** `platformio.ini` (build environments, flags, dependencies), `extra_scripts.py` (toolchain PATH fix + PROGMEM header generation), `tools/gen_config_templates.py` (template embedding), `src/config_templates_gen.cpp` (generated template wrapper).
**Delegates to:** PlatformIO, ESP-IDF CMake builder, pioarduino platform fork.
**Consumed by:** All source files (via `-I` include flags), `cfgcore::load` (`config_core.cpp:159`).

## 2. Layer Mapping

| Layer | Directory | Role |
|---|---|---|
| build | root | `platformio.ini`, `extra_scripts.py`, `partitions.csv` |
| build | `tools/` | `gen_config_templates.py`, `sign_ota.py` |
| generated | `src/generated/` | Auto-generated PROGMEM headers (gitignored) |
| templates | `templates/` | Per-board runtime default values |

## 3. Source Files

| File | Role |
|---|---|
| `platformio.ini` | Build environments, compiler flags, dependencies, partition table |
| `extra_scripts.py` | SCons extra script: toolchain PATH fix, PROGMEM header generation |
| `tools/gen_config_templates.py` | Embeds `templates/*.ini` → `src/generated/config_templates.gen.h` |
| `src/config_templates_gen.cpp` | Committed wrapper that includes the generated header |
| `partitions.csv` | OTA partition layout (`board_build.partitions = min_spiffs.csv`) |
| `templates/*.ini` | Per-board default config values |

## 4. Data Structures

### Build Flags (platformio.ini:15-54)

| Flag | Value | Description |
|---|---|---|
| `CONFIG_ASYNC_TCP_STACK_SIZE` | 16384 | AsyncTCP task stack (16 KB) |
| `CONFIG_ASYNC_TCP_QUEUE_SIZE` | 128 | AsyncTCP queue depth |
| `CONFIG_ASYNC_TCP_RUNNING_CORE` | 0 | AsyncTCP on core 0 (isolates RDM on core 1) |
| `CONFIG_ASYNC_TCP_PRIORITY` | 10 | AsyncTCP task priority |
| `CONFIG_LUXDMX_MAX_OUTPUTS` | 4 | Maximum DMX outputs |
| `CONFIG_LUXDMX_MAX_SENDERS` | 16 | Maximum active senders |
| `CONFIG_LUXDMX_RDM_TOD_MAX` | 64 | RDM Table of Devices max |
| `CONFIG_LUXDMX_LOG_BUF_CAP` | 32 | Syslog/log buffer capacity |

### Build Environments (platformio.ini:62-179)

| Env | Board | MCU | Framework | Network | Outputs | Key Flags |
|---|---|---|---|---|---|---|
| `esp32dev` | esp32dev | ESP32 | Precompiled | WiFi + W5500 SPI | 2 | `HAS_WIRED_ETH`, `HAS_ETH_SPI`, `OTA_SIGN_ENABLED=0` |
| `esp32s3dev` | esp32-s3-devkitc-1 | ESP32-S3 | From-source | WiFi | 2 | `CONFIG_ESP_BROWNOUT_DET=n` (custom_sdkconfig), `OTA_SIGN_ENABLED=0` |
| `wt32eth01` | wt32-eth01 | ESP32 | Precompiled | RMII LAN8720 | 2 | `HAS_WIRED_ETH`, `HAS_ETH_RMII`, `OTA_SIGN_ENABLED=0` |
| `esp32s3_psram` | esp32-s3-devkitc-1 | ESP32-S3 | From-source | WiFi | 2 | `CONFIG_SPIRAM_*`, `OTA_SIGN_ENABLED=0` |
| `esp32s3_n16r8_eth` | esp32-s3-devkitc-1 | ESP32-S3 | From-source | W5500 SPI | 4 | `BOARD_LUXDMX_4UNI`, `LUXDMX_SOAK_TEST`, `CONFIG_ETH_W5500=y` |
| `unit-test` | native | Host | native | N/A | N/A | `test_build_src = yes` |

### Per-Environment Build Flags (shared `build_flags` at platformio.ini:46-54)

All environments inherit the shared `build_flags` via `${env.build_flags}` and append:
- `-Iinclude -Isrc/cfg -Isrc/drv -Isrc/core -Isrc/app -Isrc/sys -Isrc/net`
- `-DDEFAULT_TEMPLATE=<board>` (selects the active template from `templates/*.ini`)
- `-DOTA_SIGN_ENABLED=0` (dev builds skip Ed25519 verification)
- Optional: `-DHAS_WIRED_ETH -DHAS_ETH_SPI` (ESP32/WROVER builds)
- Optional: `-DLUXDMX_SOAK_TEST` (4-universe env)

## 5. Concurrency

The build system itself is single-threaded (PlatformIO + SCons). The concurrency model is defined by runtime FreeRTOS tasks (see [sys-tasks](./sys-tasks.md)), configured via `build_flags` in `platformio.ini:46-54`.

## 6. State Machine

No state machine. The build is a linear pipeline: pre-build → toolchain PATH fix → PROGMEM header generation → template embedding → compile → link → (upload/monitor/test).

## 7. Entry Points

| Command | platformio.ini target/env | Purpose |
|---|---|---|
| `pio run -e esp32s3_psram` | `[env:esp32s3_psram]` | Primary build + test gate |
| `pio run -e esp32s3_psram --target upload --upload-port COM7` | same | Flash via USB |
| `pio run -e esp32s3_psram --target monitor --upload-port COM7` | same | Serial monitor |
| `pio test -e unit-test` | `[env:unit-test]` | Unity unit tests |
| `python3 test/native/test_native.py <name>` | native env | Native smoke test runner |
| `cd test/native && ./run_all.sh` | — | Native smoke test wrapper |
| `python3 tools/gen_config_templates.py` | — | Manual template regeneration |

## 8. Data Flow

### Build Pipeline

1. **Toolchain PATH fix** (extra_scripts.py:14-29): Locate `toolchain-xtensa-esp-elf` in `~/.platformio/packages/` or `tools/`, prepend `xtensa-esp-elf/bin` to PATH
2. **Source file collection**: PlatformIO compiles all `.cpp` files under `src/` per the `src/inc` include flags
3. **PROGMEM header generation** (extra_scripts.py:91-137): Gzip `src/pages/*.html` → `src/generated/*_html.h`, embed `src/assets/*` → `src/generated/*`_bin.h`
4. **Template generation** (extra_scripts.py:131-135 → gen_config_templates.py): Read `templates/*.ini`, strip comments, emit `src/generated/config_templates.gen.h` as C string literals
5. **Compile**: `xtensa-esp-elf-gcc 14.2.0` compiles firmware sources
6. **Link**: Output ELF → BIN via `esptool.py`
7. **Test (native)**: GCC compiles `test/native/*_test.cpp` + `src/core/*.cpp` + `src/cfg/*.cpp` + `src/config_templates_gen.cpp` + `src/test_stubs.cpp` against shims

### Template Resolution Flow

1. `templates/_base.ini` → global defaults (extends= not set, base template)
2. `templates/<board>.ini` → `extends=_base`, overlays board-specific pins/defaults
3. `gen_config_templates.py:40` → sorts templates with `_base` first
4. `gen_config_templates.py:50-54` → emits each template as a static C string
5. `gen_config_templates.py:61-62` → builds `CONFIG_TEMPLATES[]` registry + count
6. `config_core.cpp:157-160` → `resetToTemplate()` calls `applyNeutral()` then `applyTemplate(DEFAULT_TEMPLATE)`
7. `config_core.cpp:167-211` → `load()` overlays NVS values on top of template

## 9. Protocol Layout

N/A (no wire protocol — the build system produces firmware binaries).

## 10. Configuration Integration

Build-time configuration is done via `platformio.ini`:

| Setting | Source | Description |
|---|---|---|
| `DEFAULT_TEMPLATE` | `build_flags` per env | Selects the active template (`esp32dev`, `esp32s3dev`, `esp32s3_psram`, `luxdmx_4uni`) |
| `HAS_WIRED_ETH` | `build_flags` per env | Compiles wired Ethernet code paths |
| `HAS_ETH_SPI` | `build_flags` per env | W5500 SPI Ethernet driver |
| `HAS_ETH_RMII` | `build_flags` per env (wt32eth01) | LAN8720 RMII Ethernet driver |
| `OTA_SIGN_ENABLED` | `build_flags` per env | Ed25519 signature verification (0=dev, 1=production) |
| `LUXDMX_SOAK_TEST` | `build_flags` (n16r8_eth only) | 60 s heap watchdog + `/diag/soak-stats` |
| `BOARD_LUXDMX_4UNI` | `build_flags` (n16r8_eth only) | 4-universe pin map + copper-pin locks |
| `BOARD_LUXDMX_V6` | `build_flags` (optional) | v6 board defaults |

## 11. Lifecycle

1. **Pre-build:** `extra_scripts.py` runs (SCons hook): PATH fix + header generation
2. **Build:** `pio run -e <env>` compiles all sources
3. **Flash (optional):** `pio run -e <env> --target upload`
4. **Monitor (optional):** `pio run -e <env> --target monitor`
5. **Test:** `pio test -e unit-test` or native `python3 test/native/test_native.py <name>`

No explicit shutdown/cleanup phase in the build system.

## 12. Error Handling

| Condition | Handling | Source |
|---|---|---|
| `esp-modbus` Kconfig conflict | `custom_component_remove = espressif/esp-modbus` | `platformio.ini:94,154,178` |
| S3 brownout before setup() | `custom_sdkconfig CONFIG_ESP_BROWNOUT_DET=n` | `platformio.ini:86-87,145-146` |
| libatomic.a missing on S3 | Avoided by not using `std::shared_ptr`/`<atomic>`; see `platformio.ini:56-60` note | `platformio.ini:56-60` |
| Toolchain PATH | Prepends nested `xtensa-esp-elf/bin` to PATH | `extra_scripts.py:20-28` |
| Include path collision | `-Isrc/sys` NOT added to S3 from-source builds (shadows esp-mqtt `platform.h`) | `platformio.ini:73,95,155,167` |
| W5500 not in build | `HAS_ETH_SPI` gate; `ethernet.cpp:105` warns | `ethernet.cpp:105` |

## 13. Memory Allocation

- **Build-time static PROGMEM:** Gzipped HTML/JS/CSS embedded as `static const uint8_t[]` arrays (extra_scripts.py:55-58)
- **Runtime heap:** Controlled by `CONFIG_SPIRAM_*` flags (S3 PSRAM builds) — WiFi/lwIP buffers in PSRAM via `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`
- **Partitions:** `board_build.partitions = min_spiffs.csv` (platformio.ini:36) — OTA dual-slot + SPIFFS

## 14. Timing

No build-time timing constraints. Runtime timing is controlled by FreeRTOS task periods (see [sys-tasks](./sys-tasks.md)).

## 15. Traceability / Evidence

| Claim | Source |
|---|---|
| 5-layer model in platformio.ini comment | `platformio.ini:2` |
| AsyncTCP hardening: core 0, stack 16384, queue 128, prio 10 | `platformio.ini:46-50` |
| Measured RDM TX→RX inflation 15→53 µs with AsyncTCP on core 1 | `platformio.ini:42-45` |
| esp32dev: `HAS_WIRED_ETH`, `HAS_ETH_SPI`, `OTA_SIGN_ENABLED=0` | `platformio.ini:73` |
| esp32s3dev: `CONFIG_ESP_BROWNOUT_DET=n` (boot-loop fix) | `platformio.ini:86-87` |
| esp32s3dev: `custom_component_remove = espressif/esp-modbus` | `platformio.ini:94` |
| esp32s3_psram: octal PSRAM config (`CONFIG_SPIRAM_MODE_OCT=y`, `CONFIG_SPIRAM_SPEED_80M=y`) | `platformio.ini:145-153` |
| esp32s3_n16r8_eth: `LUXDMX_SOAK_TEST`, `CONFIG_ETH_W5500=y` | `platformio.ini:167,177` |
| S3 builds run from-source (no precompiled) due to brownout fix | `platformio.ini:81-85` |
| Include path collision: `-Isrc/sys` would shadow esp-mqtt `platform.h` | `platformio.ini:56-60` (comment) |
| Toolchain PATH fix for nested `xtensa-esp-elf/bin` | `extra_scripts.py:20-28` |
| PROGMEM header generation: gzip HTML + binary assets | `extra_scripts.py:91-137` |
| Template generation: `tools/gen_config_templates.py` runs from extra_scripts | `extra_scripts.py:131-135` |
| `_base` template sorted first | `gen_config_templates.py:40` |
| Template → `CONFIG_TEMPLATES[]` registry | `gen_config_templates.py:54-62` |
| `config_templates_gen.cpp` includes generated header | `src/config_templates_gen.cpp:7` |
| Native test build: `build_src_filter` selects specific .cpp files | `platformio.ini:204-214` |

## 16. Cross-References

- [INDEX](./INDEX.md)
- [Architecture Overview](./architecture-overview.md)
- [Config Engine](./config-engine.md)
- [Include Headers](./include-headers.md)
- [Test Infrastructure](./test-infrastructure.md)
- [Sys Tasks](./sys-tasks.md)
- [Crash Guard](./sys-crash-guard.md)

## 17. Limitations

- Only `esp32s3_psram` is the validated build gate — other environments (esp32dev, esp32s3dev, wt32eth01, esp32s3_n16r8_eth) compile but are not in the CI gate.
- The `test/native/run_all.sh` and `run_all.bat` reference `build/test_native.py` which does not exist in the repository (see [Test Infrastructure](./test-infrastructure.md) Section 17, Limitations).
- The native test `gen_config_templates.py:11` references `test/native/run.bat` which also does not exist.
- From-source S3 builds lack `libatomic.a` — `std::shared_ptr`/`<atomic>` usage would fail (see `platformio.ini:56-60`).

## 18. Open Questions

- Not determinable from the inspected source code — whether the `build/test_native.py` referenced by run scripts is generated elsewhere or is a known broken reference.
- Not determinable from the inspected source code — the exact `min_spiffs.csv` partition layout values (defined in a file not inspected).

## 19. Testing

| Test Framework | Runner | Config Source |
|---|---|---|
| Native smoke tests | `test/native/run_all.sh` / `run_all.bat` → `python3 test/native/test_native.py <name>` | `test/native/test_native.py` (missing from repo) |
| Unity unit tests | `pio test -e unit-test` | `platformio.ini:182-214` |

The `unit-test` environment compiles a selective subset of source files via `build_src_filter` (`platformio.ini:204-214`): `merge_engine.cpp`, `dmx_buffer.cpp`, `sender_tracker.cpp`, `stats.cpp`, `config_schema.cpp`, `config_core.cpp`, `config_serial.cpp`, `config_templates_gen.cpp`, `test_stubs.cpp`.

See [Test Infrastructure](./test-infrastructure.md) for the full test framework documentation.

## 20. History

| Date | Change | Source |
|---|---|---|
| 2026-08-15 | AsyncTCP hardening block added (core 0 isolation) | `platformio.ini:38-50` |
| 2026-08-15 | S3 brownout detector disabled via `custom_sdkconfig` | `platformio.ini:86-87` |
| 2026-08-15 | `esp-modbus` component removed to avoid Kconfig conflict | `platformio.ini:94` |
| 2026-08-15 | Include path (`-I`) flags moved from `build_flags` to per-env for S3 builds | `platformio.ini:56-60` (comment) |
| 2026-08-15 | 4-universe env (`esp32s3_n16r8_eth`) added with PSRAM + W5500 + soak test | `platformio.ini:157-179` |
