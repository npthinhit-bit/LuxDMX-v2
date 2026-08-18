# Plan: Add Embedded Testing to LuxDMX-v2

## Goal

Add an embedded testing tier that runs Unity tests on ESP32-S3 hardware
(Wokwi simulation for CI, real device locally), covering the hardware-bound
modules that native/host tests cannot exercise.

## Background & Rationale

The project currently has two testing tiers, both **host-based**:

| Tier | Location | Framework | Target | Modules |
|---|---|---|---|---|
| Native smoke | `test/native/` | `CHECK` macro, no framework | Host (GCC/MSVC) | config, seqlock, merge, rdm_types |
| Unity unit | `test/unit-test/` | Unity | Host (`native` platform) | same modules |

**Gap**: Zero test coverage for hardware-bound modules. Documentation explicitly states:
- `docs/SYSTEM_SPECIFICATION/14-dmx-rmt-tx-spec.md:163`: "No host-native or hardware test coverage exists"
- `docs/SYSTEM_SPECIFICATION/15-dmx-uart-rx-spec.md:190`: "No host-native or hardware test coverage exists"
- `docs/SYSTEM_SPECIFICATION/16-gpio-dir-spec.md:145`: "No host-native or hardware test coverage exists"
- `docs/TECHNICAL_REFERENCE/core-rdm-engine.md:295`: "No host tests exist for `rdm_engine.cpp` transport functions"
- `docs/TECHNICAL_REFERENCE/core-rdm-task.md:154`: No host test covers task dispatch

These modules contain testable pure-logic (packet assembly, byte packing, symbol
encoding, binary search parsing) that is currently only validated by manual hardware
testing described in `CLAUDE.md`'s "Firmware Evaluation Workflow".

## Design Decisions

1. **Framework**: Unity (same as existing `[env:unit-test]`)
2. **CI target**: Wokwi ESP32-S3 simulation (no hardware needed in CI)
3. **Local target**: Real ESP32-S3 hardware via `pio test -e esp32s3_test`
4. **Test isolation**: `build_src_filter` includes only needed source files (same pattern
   as `[env:unit-test]` at `platformio.ini:204-214`)
5. **Stubs**: Update `src/test_stubs.cpp` guard from `#ifdef UNIT_TESTING` to
   `#if defined(UNIT_TESTING) || defined(EMBEDDED_TESTING)` so no-op stubs are
   available for embedded test builds that include modules calling `alertSourceLost`,
   `sceneRecall`, etc.
6. **Build gate**: Any source change (including `test_stubs.cpp`) must pass
   `pio run -e esp32s3_psram` before being considered complete.

## Test Specification

### Structure

```
test/embedded/
  test_suite/
    test_rdm_transport.cpp    # rdmBuild, putUid (pure logic)
    test_rdm_disc.cpp          # uidPack, uidUnpack, disc response parsing
    test_dmx_rmt.cpp           # rmtDmxEncode (symbol encoding, break/MAB)
    test_gpio_dir.cpp          # gpioDeSet, gpioDeInit (GPIO direction)
    test_config.cpp            # Config template loading + NVS on real NVS
    test_integration.cpp       # Output init + snapshot cycle on device
```

Single test directory (`test_suite`) produces one firmware binary for Wokwi,
matching the single-binary approach used by the existing `[env:unit-test]`.

### Test Cases

**test_rdm_transport.cpp** (`rdm_engine.cpp` — `rdmBuild`, `putUid`):
- `test_rdm_build_layout` — verify packet: SC=0xCC, SC_SUB=0x01, length=24+pdl, dest UID at [3..8], src UID at [9..14], CC at [21], PID at [22..23], PDL at [24]
- `test_rdm_build_checksum` — verify additive 8-bit checksum over all bytes excluding checksum itself
- `test_rdm_build_transaction_increments` — verify `g_rdm.tn` increments per call (wrapping at 255)
- `test_rdm_build_no_param_data` — verify PDL=0 request has correct length and no trailing data
- `test_put_uid_big_endian` — verify UID is packed MSB-first: man_id >> 8, man_id & 0xff, dev_id >> 24, ...

**test_rdm_disc.cpp** (`rdm_disc.cpp` — `uidPack`, `uidUnpack`):
- `test_uid_pack_roundtrip` — pack then unpack preserves man_id + dev_id
- `test_uid_pack_order` — verify pack produces (man_id << 32) | dev_id
- `test_uid_pack_broadcast` — verify `RDM_UID_BROADCAST_ALL` packs/unpacks correctly
- `test_uid_pack_max` — verify `RDM_UID_MAX` packs/unpacks correctly

**test_dmx_rmt.cpp** (`dmx_rmt.h` — `rmtDmxEncode`):
- `test_rmt_dmx_encode_break_mab` — verify first symbol word has break duration (176) low + MAB (12) high
- `test_rmt_dmx_encode_byte_0x00` — verify encoding of start bit + 8 zero data + 2 stop bits produces correct run-length
- `test_rmt_dmx_encode_byte_0xff` — verify encoding of start bit + 8 one data + 2 stop bits
- `test_rmt_dmx_encode_invert` — verify `rd->invert` swaps levels in symbol buffer
- `test_rmt_dmx_encode_length` — verify `rd->nsym` matches expected word count for a known frame

**test_gpio_dir.cpp** (`gpio_dir.h` — `gpioDeSet`, `gpioDeInit`):
- `test_gpio_set_high` — call `gpioDeSet(pin, 1)`, read back via `gpio_get_level`
- `test_gpio_set_low` — call `gpioDeSet(pin, 0)`, read back via `gpio_get_level`
- `test_gpio_init_output` — call `gpioDeInit(pin)`, verify pin is configured as output and high

**test_config.cpp** (`cfg/config_core.cpp`, `cfg/config_schema.cpp`):
- `test_config_defaults` — verify `_base.ini` template values (hostname="dmx-gateway", protocol=2)
- `test_config_nvs_roundtrip` — set value → save to NVS → reset → load → verify (uses real NVS, not shim)

**test_integration.cpp** (`core/rdm_engine.cpp`, `core/stats.cpp`):
- `test_rdm_state_zero_init` — verify `g_rdm` zero-initializes (lineN=0, lineN=0)
- `test_rdm_ctrl_uid_default` — verify controller UID man_id = 0x4C58 before `rdmInitCtrlUid`

## PlatformIO Environment

Add to `platformio.ini` (after existing `[env:unit-test]` at line 214):

```ini
[env:esp32s3_test]
platform = ${env:esp32s3dev.platform}
board = esp32-s3-devkitc-1
framework = arduino
test_dir = test/embedded
test_framework = unity
test_build_src = yes
custom_sdkconfig =
    CONFIG_ESP_BROWNOUT_DET=n
custom_component_remove = espressif/esp-modbus
build_flags =
    ${env.build_flags}
    -Iinclude
    -Isrc/cfg
    -Isrc/drv
    -Isrc/core
    -Isrc/app
    -Isrc/sys
    -Isrc/net
    -Isrc/generated
    -DEMBEDDED_TESTING
    -DDEFAULT_TEMPLATE=esp32s3dev
    -DOTA_SIGN_ENABLED=0
    -DCONFIG_LUXDMX_MAX_OUTPUTS=2
lib_deps =
    unity
build_src_filter =
    -<*>
    -<main.cpp>
    +<test_stubs.cpp>
    +<drv/dmx_rmt_lut.cpp>
    +<core/rdm_engine.cpp>
    +<core/rdm_disc.cpp>
    +<core/stats.cpp>
    +<cfg/config_schema.cpp>
    +<cfg/config_core.cpp>
    +<cfg/config_serial.cpp>
    +<config_templates_gen.cpp>
```

Rationale for `build_src_filter`: excludes everything by default, then includes only the
source files needed by the test modules — same approach as `[env:unit-test]` but targeting
the real ESP32-S3 toolchain instead of `native`.

## Wokwi Configuration

Create `wokwi.toml` at project root:

```toml
[wokwi]
version = 1
board = "esp32-s3-devkitc-1"
```

Wokwi simulates GPIO and UART for the ESP32-S3, enabling:
- `gpioDeSet`/`gpioDeInit` tests (GPIO level readback)
- `rdmBuild`/`putUid` tests (pure logic, no hardware needed)
- `rmtDmxEncode` tests (buffer manipulation only, no RMT hardware)
- `uidPack`/`uidUnpack` tests (pure logic)
- Config NVS round-trip (Wokwi provides NVS flash simulation)

Wokwi does **not** simulate the RMT peripheral, so tests that call `rmt_transmit`
(e.g., `rdmTx` inside `rdmDiscBranch`) will only run on real hardware. These tests
are structured to verify pure-logic paths on Wokwi and hardware-interaction paths
on real devices.

## CI Integration

Add to `.github/workflows/ci.yml` (new job, after `unity-tests`):

```yaml
  embedded-tests:
    name: Embedded Tests (Wokwi)
    runs-on: ubuntu-latest
    needs:
      - build
      - native-tests
      - unity-tests
    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Setup Python
        uses: actions/setup-python@v5
        with:
          python-version: ${{ env.PYTHON }}

      - name: Install PlatformIO
        run: |
          python -m pip install --upgrade pip
          pip install platformio

      - name: Generate config templates
        run: python3 tools/gen_config_templates.py .
        shell: bash

      - name: Build embedded test firmware
        run: pio test -e esp32s3_test --target build
        shell: bash

      - name: Run Wokwi simulation
        uses: wokwi/wokwi-github-action@v1
        with:
          target: esp32-s3-devkitc-1
          bin: .pio/build/esp32s3_test/test_suite/firmware.bin
```

**Binary path**: PlatformIO places test build output at
`.pio/build/<env>/<test_name>/firmware.bin`. The test directory name (`test_suite`)
determines `<test_name>`. If the path differs, adjust the `bin` parameter.

## Local Runner Scripts

Create `scripts/run_embedded_tests.sh` (Linux/macOS):

```bash
#!/bin/bash
# Build and run embedded tests on real ESP32-S3 hardware or Wokwi.
set -e
...
pio test -e esp32s3_test --verbose     # real hardware via USB
# OR (if wokwi installed): wokwi --bin .pio/build/esp32s3_test/test_suite/firmware.bin
```

Create `scripts/run_embedded_tests.ps1` (Windows):

```powershell
# Build and run embedded tests on real ESP32-S3 hardware.
pio test -e esp32s3_test --verbose
```

## Affected Files

| File | Action | Notes |
|---|---|---|
| `platformio.ini` | Edit | Add `[env:esp32s3_test]` after line 214 |
| `src/test_stubs.cpp` | Edit | Change `#ifdef UNIT_TESTING` → `#if defined(UNIT_TESTING) \| defined(EMBEDDED_TESTING)` |
| `test/embedded/test_suite/test_rdm_transport.cpp` | Create | 5 test cases (rdmBuild, putUid) |
| `test/embedded/test_suite/test_rdm_disc.cpp` | Create | 4 test cases (uidPack/unpack) |
| `test/embedded/test_suite/test_dmx_rmt.cpp` | Create | 5 test cases (rmtDmxEncode) |
| `test/embedded/test_suite/test_gpio_dir.cpp` | Create | 3 test cases (gpioDeSet/Init) |
| `test/embedded/test_suite/test_config.cpp` | Create | 2 test cases (template, NVS) |
| `test/embedded/test_suite/test_integration.cpp` | Create | 2 test cases (state, UID default) |
| `wokwi.toml` | Create | ESP32-S3 DevKitC-1 board config |
| `.github/workflows/ci.yml` | Edit | Add `embedded-tests` job |
| `scripts/run_embedded_tests.sh` | Create | Local Linux/macOS runner |
| `scripts/run_embedded_tests.ps1` | Create | Local Windows runner |
| `docs/TECHNICAL_REFERENCE/test-infrastructure.md` | Edit | Add embedded test section |

## Validation

1. **Build gate**: `pio run -e esp32s3_psram` passes after all changes (project constraint)
2. **Unity build**: `pio test -e esp32s3_test --target build` compiles successfully
3. **Wokwi simulation**: All tests pass in Wokwi CI
4. **Real hardware**: Tests pass via `pio test -e esp32s3_test` on ESP32-S3
5. **No regression**: Existing `pio test -e unit-test` and `test/native/test_native.py all` still pass

## Risks & Mitigations

| Risk | Mitigation |
|---|---|
| RMT peripheral not simulated in Wokwi | Test only pure-logic paths (rmtDmxEncode buffer encoding) on Wokwi; hardware TX/RX tests remain hardware-only |
| `rdm_engine.cpp` depends on `esp_read_mac`, NVS, real ESP-IDF headers | These are available on the ESP32-S3 Arduino target — no shims needed (opposite of native test approach) |
| `build_src_filter` missing a dependency → link error | Start with minimal file set; iterate by building and adding missing files |
| Wokwi binary path differs from expected | CI step includes `find` fallback to locate `.bin` dynamically |
| NVS persistence between test runs on Wokwi | Test resets NVS via `Preferences::clearAll()` or uses unique keys per test |
| `test_stubs.cpp` change breaks native `unit-test` env | Change uses `#if defined(UNIT_TESTING) \|\| defined(EMBEDDED_TESTING)` — native env still defines `UNIT_TESTING`, behavior unchanged |

## Out of Scope

- No changes to production firmware source code (only `test_stubs.cpp` guard)
- No hardware-only tests requiring real DMX fixtures (RDM responder, DMX analyzer) — these remain manual per `CLAUDE.md` "Firmware Evaluation Workflow"
- No test infrastructure for sACN, Art-Net, WebSocket, or OTA modules (would require network simulation in Wokwi)
- No migration of existing native/Unity tests to embedded target
