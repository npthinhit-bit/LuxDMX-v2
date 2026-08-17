Test Infrastructure — Technical Reference

Domain: `system.test`

## 1. Domain Scope

Two-tier test infrastructure for the LuxDMX-v2 firmware:

1. **Native smoke tests** (`test/native/`) — standalone C++ programs compiled with GCC/MSVC against shim headers. No external framework. Each test is a `main()` function with a `CHECK` macro.
2. **Unity unit tests** (`test/unit-test/`) — PlatformIO `pio test -e unit-test` using the Unity framework. Tests use `TEST_ASSERT_*` macros with `setUp`/`tearDown` fixtures.

The native tests verify config engine, seqlock, merge engine, and RDM types without hardware. The Unity tests run the same modules with the Unity framework and a `Preferences` shim for NVS round-tripping.

**Owns:** Test runner scripts (`run_all.sh`, `run_all.bat`), shim headers, test entry points.
**Delegates to:** `src/core/` and `src/cfg/` modules under test.
**Consumed by:** `platformio.ini` `[env:unit-test]` configuration.

## 2. Layer Mapping

| Layer | Directory | Role |
|---|---|---|
| test | `test/` | Test infrastructure |
| test | `test/native/` | Native smoke tests (no framework) |
| test | `test/unit-test/` | Unity unit tests |
| test | `test/native/shim/` | Arduino/ESP-IDF shims for host compilation |

## 3. Source Files

| File | Role |
|---|---|
| `test/native/config_test.cpp` | Config engine smoke test (template, setValue/getValue, NVS round-trip, serial console) |
| `test/native/seqlock_test.cpp` | Seqlock smoke test (snapshot, retry, 100 cycles) |
| `test/native/merge_test.cpp` | Merge engine smoke test (HTP, OFF, LOSS_ZERO, LTP-Takeover, priority, cross-universe) |
| `test/native/rdm_types_test.cpp` | RDM types smoke test (UID, enums, PID constants) |
| `test/native/run_all.sh` | Bash test runner: 4 tests via `python3 build/test_native.py` |
| `test/native/run_all.bat` | Windows test runner: 4 tests via `python build\test_native.py` |
| `test/native/shim/Arduino.h` | `String` class, `millis()`, `constrain()`, `__sync_synchronize`, `__attribute__` |
| `test/native/shim/Preferences.h` | In-memory `Preferences` map for NVS round-trip |
| `test/native/shim/esp_err.h` | `ESP_OK`, `ESP_ERR_*` constants |
| `test/native/shim/esp_log.h` | `ESP_LOGE` stub |
| `test/native/shim/esp_heap_caps.h` | `heap_caps_malloc` stub |
| `test/native/shim/driver/rmt_tx.h` | RMT types/stubs |
| `test/native/shim/driver/uart.h` | UART port stubs |
| `test/native/unity_host.c` | Unity host-side C glue |
| `test/unit-test/test_config/test_unit_config.cpp` | Unity: config defaults, set/get, NVS, templates, serial |
| `test/unit-test/test_merge/test_unit_merge.cpp` | Unity: merge engine |
| `test/unit-test/test_rdm_types/test_unit_rdm_types.cpp` | Unity: RDM types |
| `test/unit-test/test_seqlock/test_unit_seqlock.cpp` | Unity: seqlock |
| `src/test_stubs.cpp` | Stub implementations for `alert.cpp`, `scene_engine.cpp` symbols used in native tests |
| `platformio.ini:182-214` | `[env:unit-test]` Unity native environment |

## 4. Data Structures

### Native Smoke Test Framework

No external framework. Uses a local `CHECK` macro:

```cpp
static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("  FAIL: %s\n", msg); } } while (0)
```

Each test is a standalone `int main()` that returns `g_fail ? 1 : 0`.

### Unity Unit Test Framework

Uses the Unity framework (`lib_deps = unity` in `platformio.ini:203`). Tests use `TEST_ASSERT_*` macros with `setUp()`/`tearDown()` fixtures. Tests are registered via `RUN_TEST()` in `main()`.

### Preferences Shim (test/native/shim/Preferences.h:14-94)

| Component | Implementation |
|---|---|
| Storage backend | `static std::map<std::string, std::string>` |
| `begin(ns, rw)` | No-op, returns `true` |
| `getBool`/`putBool` | String "true"/"false" |
| `getInt`/`putInt` | `atoi`/`snprintf` |
| `getBytes`/`putBytes` | Binary copy into map |
| `clearAll()` | Static method: `storage().clear()` |

### millis() Shim (Arduino.h:76)

```cpp
inline uint32_t millis() { static uint32_t t = 0; return t++; }
```

Returns incrementing 0, 1, 2, ... — deterministic for test reproducibility.

## 5. Concurrency

Single-threaded. Both native and Unity tests run in a single thread on the host. No FreeRTOS, no task scheduling. The `__sync_synchronize()` shim (`Arduino.h:72`) is a no-op compiler barrier (`_ReadWriteBarrier()` on MSVC, nothing on GCC).

## 6. State Machine

No state machine. Tests are linear: setup → exercise → assert → teardown.

## 7. Entry Points

| Test | Entry | Framework | Compiled Sources |
|---|---|---|---|
| `config_test` | `test/native/config_test.cpp:10` | None (CHECK macro) | config_core, config_schema, config_templates_gen |
| `seqlock_test` | `test/native/seqlock_test.cpp:16` | None | seqlock.h (header-only) |
| `merge_test` | `test/native/merge_test.cpp:25` | None | merge_engine, dmx_buffer, sender_tracker |
| `rdm_types_test` | `test/native/rdm_types_test.cpp:9` | None | rdm_types.h (header-only) |
| `test_config` | `test/unit-test/test_config/test_unit_config.cpp:77` | Unity | config_core, config_schema, config_serial, config_templates_gen, test_stubs |
| `test_merge` | `test/unit-test/test_merge/test_unit_merge.cpp:?` | Unity | merge_engine, dmx_buffer, sender_tracker |
| `test_rdm_types` | `test/unit-test/test_rdm_types/test_unit_rdm_types.cpp:?` | Unity | rdm_types.h |
| `test_seqlock` | `test/unit-test/test_seqlock/test_unit_seqlock.cpp:?` | Unity | seqlock.h |

## 8. Data Flow

### Native Smoke Test Flow

1. `run_all.sh:7` loops over 4 test names: `config_test seqlock_test merge_test rdm_types_test`
2. For each: `python3 build/test_native.py "$t"` (run_all.sh:9)
3. `test_native.py` (missing from repo — see Limitations) compiles the test with GCC, links against shim headers, and runs the binary
4. Each test calls `cfgcore::resetToTemplate()` (config_test.cpp:12) to load defaults
5. `CHECK` assertions verify behavior; test prints "N passed, M failed" and returns exit code

### Unity Test Flow

1. `pio test -e unit-test` (platformio.ini:182-214)
2. `build_src_filter` (platformio.ini:204-214) selects only:
   - `core/merge_engine.cpp`, `core/dmx_buffer.cpp`, `core/sender_tracker.cpp`, `core/stats.cpp`
   - `cfg/config_schema.cpp`, `cfg/config_core.cpp`, `cfg/config_serial.cpp`, `config_templates_gen.cpp`
   - `test_stubs.cpp`
3. `setUp()` (test_unit_config.cpp:7-10) clears Preferences and resets to template
4. `RUN_TEST()` executes each test function
5. `UNITY_BEGIN()` / `UNITY_END()` framework handles reporting

## 9. Protocol Layout

N/A (no wire protocol).

## 10. Configuration Integration

The native tests depend on the config template system:

| Config Aspect | Test Usage |
|---|---|
| `DEFAULT_TEMPLATE` | `config_test.cpp:13` checks `hostname == "dmx-gateway"` (from `_base.ini:9`) |
| `luxdmx_4uni` template | `config_test.cpp:19` checks output A/B RDM-capable, C/D DMX-only |
| NVS round-trip | `config_test.cpp:40-44` save → reset → load → verify |
| Serial console | `config_test.cpp:49-55` dump/get/set commands via `cfgserial::execute` |

## 11. Lifecycle

1. **Pre-test:** `extra_scripts.py` generates `config_templates.gen.h` (needed by `config_templates_gen.cpp`)
   - For native tests: `gen_config_templates.py:11` references `test/native/run.bat` (which does not exist — broken reference)
   - For Unity: automatic via PlatformIO build system
2. **Compile:** PlatformIO (`pio test`) or manual script (`test_native.py`)
3. **Run:** Test binary executes `main()` → assertions → exit code
4. **No deinit** — tests exit after `main()` returns

## 12. Error Handling

| Condition | Handling | Source |
|---|---|---|
| Test assertion failure | `g_fail++`, prints "FAIL: msg", returns exit code 1 | `config_test.cpp:8` |
| Unknown config key | `cfgcore::setValue` returns `ESP_ERR_INVALID_ARG`, sets `err` | `config_core.cpp:90` |
| NVS key missing | `Preferences` shim returns default value | `test/native/shim/Preferences.h` |
| Unity test failure | `TEST_ASSERT_*` macro fails, Unity reports | `test_unit_config.cpp` |

## 13. Memory Allocation

- **Native tests:** Stack-allocated `Frame` structs, static `g_pass`/`g_fail` counters, `std::map` in Preferences shim
- **Unity tests:** Stack-allocated test data, `Preferences` shim backing store
- **Preferences shim:** `std::map<std::string, std::string>` on host heap
- No `heap_caps` or PSRAM in test mode (shim stubs return success)

## 14. Timing

Tests are deterministic and single-threaded. The `millis()` shim returns incrementing integers starting from 0 (`Arduino.h:76`), so timing-dependent assertions are reproducible. No real-time constraints.

## 15. Traceability / Evidence

| Claim | Source |
|---|---|
| Native tests use `CHECK` macro, no framework | `config_test.cpp:8` |
| Unity tests use `TEST_ASSERT_*` macros | `test_unit_config.cpp:13-15` |
| `run_all.sh` runs 4 tests via `python3 build/test_native.py` | `run_all.sh:6,9` |
| `run_all.bat` runs 4 tests via `python build\test_native.py` | `run_all.bat:8,11` |
| Unity `setUp` clears Preferences + resets template | `test_unit_config.cpp:7-10` |
| `build_src_filter` selects specific source files | `platformio.ini:204-214` |
| `test_stubs.cpp` provides stub `alertSourceLost`, `sceneRecall` | `src/test_stubs.cpp:6-17` |
| `millis()` shim returns incrementing value | `Arduino.h:76` |
| `__sync_synchronize()` shim is no-op | `Arduino.h:72` |
| Preferences shim uses `std::map` | `Preferences.h:15-17` |
| Native tests compiled against `test/native/shim/` | `platformio.ini:200` |
| `test_native.py` is referenced but NOT in the repo | `run_all.sh:9`, `run_all.bat:11` |
| `gen_config_templates.py:11` references `test/native/run.bat` | `tools/gen_config_templates.py:11` |

## 16. Cross-References

- [Config Engine](./config-engine.md) — config_core, config_schema, config_serial
- [DMX Buffer](./core-dmx-buffer.md) — seqlock tested by `seqlock_test.cpp` / `test_seqlock`
- [Merge Engine](./core-merge-engine.md) — tested by `merge_test.cpp` / `test_merge`
- [RDM Engine](./core-rdm-engine.md) — rdm_types tested by `rdm_types_test.cpp` / `test_rdm_types`
- [Build System](./build-system.md) — `[env:unit-test]` configuration
- [Include Headers](./include-headers.md) — `seqlock.h`, `rdm_types.h`, `config_schema.h`

## 17. Limitations

- **Broken test runner reference:** `run_all.sh:9` and `run_all.bat:11` call `python3 build/test_native.py` / `python build\test_native.py`, but `build/test_native.py` does not exist in the repository. Native smoke tests cannot be run via the provided scripts.
- **Broken template reference:** `gen_config_templates.py:11` references `test/native/run.bat` which does not exist (should be `run_all.bat`).
- **No native test for core/RDM modules:** `rdm_engine.cpp`, `rdm_disc.cpp`, `rdm_task.cpp`, `scene_engine.cpp`, `input_router.cpp`, `output_init.cpp`, `sender_tracker.cpp`, `frame_router.cpp` have no native or Unity tests.
- **Unity `test_stubs.cpp` is minimal:** Only stubs `alertSourceLost`, `alertSourceRestored`, `sceneRecall`, `sceneRecallHome` — any test that exercises code calling other un-stubbed symbols will fail to link.
- **`build_src_filter` only includes 8 source files** (platformio.ini:204-214) — modules like `artnet.cpp`, `ethernet.cpp`, `web_server.cpp` are excluded from Unity tests.

## 18. Open Questions

- Not determinable from the inspected source code — the intended location of `test_native.py` (the script referenced by `run_all.sh`/`run_all.bat`). It was not found in `build/`, `test/native/`, or the project root.
- Not determinable from the inspected source code — whether `test_seqlock`'s `test_unit_seqlock.cpp` tests the generic `SeqLock` or the `dmxBufSnapshot` wrapper (the file was not read in full).
- Not determinable from the inspected source code — whether `test_merge`'s `test_unit_merge.cpp` mirrors the native `merge_test.cpp` test cases (the file was not read in full).

## 19. Testing

| Test Suite | Framework | File | Tests |
|---|---|---|---|
| `config_test` | Native (CHECK) | `test/native/config_test.cpp` | 5: template resolution, luxdmx_4uni, setValue/getValue, NVS round-trip, serial console |
| `seqlock_test` | Native (CHECK) | `test/native/seqlock_test.cpp` | 3: clean snapshot, stable write, 100 cycles |
| `merge_test` | Native (CHECK) | `test/native/merge_test.cpp` | 8: HTP, OFF, LOSS_ZERO, LTP-Takeover, priority merge, cross-universe, failsafe timeouts, LOSS_PRESET/HOME fallback |
| `rdm_types_test` | Native (CHECK) | `test/native/rdm_types_test.cpp` | 10: UID equality, broadcast/max, response types, PID constants, CC constants, packet size, ASCII size, sensor types, sensor units, UID pointer compare |
| `test_config` | Unity | `test/unit-test/test_config/test_unit_config.cpp` | 8: defaults, set/get, NVS roundtrip, luxdmx_4uni template, serial set/get, serial dump, serial invalid |
| `test_merge` | Unity | `test/unit-test/test_merge/test_unit_merge.cpp` | Not fully inspected — see Open Questions |
| `test_rdm_types` | Unity | `test/unit-test/test_rdm_types/test_unit_rdm_types.cpp` | Not fully inspected — see Open Questions |
| `test_seqlock` | Unity | `test/unit-test/test_seqlock/test_unit_seqlock.cpp` | Not fully inspected — see Open Questions |

## 20. History

No recorded changes.
