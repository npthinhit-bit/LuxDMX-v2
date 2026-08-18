# Plan: Comprehensive Embedded CI/CD Pipeline for LuxDMX-v2

## Objective

Upgrade the existing 3-job CI pipeline (`ci.yml`) and release workflow (`release.yml`) into a comprehensive 5-group embedded firmware CI/CD pipeline covering: static analysis, build & footprint, automated testing, advanced & security testing, and artifacts & release management. The pipeline must gate every source change with `pio run -e esp32s3_psram` (the project's build gate), enforce quality gates with fail-fast semantics, and produce verified release artifacts.

## Current State

### Existing CI (`.github/workflows/ci.yml`)
| Job | Currently Does | Gaps |
|---|---|---|
| `build` | Matrix builds 5 envs; uploads `firmware.bin` artifacts | No flash/RAM footprint checks; no delta tracking; no `.elf` or `.map` artifacts in CI |
| `test` | Runs `python3 test/native/test_native.py all` (4 native smoke tests) | **Does NOT** run Unity tests via `pio test -e unit-test` (4 additional test suites); no code coverage collection |
| `lint` | Runs `pio check` (cppcheck) with `--src-filters="+<src/> +<include/>"` | No clang-format style check; no clang-tidy; no MISRA compliance; no dependency/CVE scanning |

### Existing Release (`.github/workflows/release.yml`)
- Builds 5 envs, uploads `.bin` + `.elf`, generates release notes from commit log
- **No SHA256 hashes** on artifacts
- **No firmware signing** step in CI (signing is manual via `tools/sign_ota.py`)

### Test Infrastructure
- **Native tests** (`test/native/`): 4 tests — `config_test`, `seqlock_test`, `merge_test`, `rdm_types_test`. Compiled with `g++` against shims in `test/native/shim/`. Runner: `test/native/test_native.py`.
- **Unity tests** (`test/unit-test/`): 4 test files — `test_config`, `test_merge`, `test_rdm_types`, `test_seqlock`. Built via `[env:unit-test]` in `platformio.ini`. **NOT currently in CI.**
- **Shims**: `Arduino.h`, `Preferences.h`, `esp_err.h`, `esp_log.h`, `esp_heap_caps.h`, `driver/rmt_tx.h`, `driver/uart.h` in `test/native/shim/`
- **Native test deps** (`test_native.py TEST_DEPS`): config_test → config_core, config_schema, config_serial, config_templates_gen, test_stubs. merge_test → merge_engine, dmx_buffer, sender_tracker, stats, config_schema, config_core, config_serial, config_templates_gen, test_stubs.

### Dependencies (platformio.ini line 28-30)
- `AsyncTCP` — git: `https://github.com/ESP32Async/AsyncTCP.git`
- `ESPAsyncWebServer` — git: `https://github.com/ESP32Async/ESPAsyncWebServer.git`
- `unity` — PlatformIO registry (unit-test env only)

## Decisions

### D1: Pipeline Structure
**Decision:** Create a single `ci.yml` with 8 sequential job stages (fail-fast within each stage, `needs:` chaining between stages). Keep `release.yml` for tag-triggered releases with signing + SHA256.

Jobs (ordered):
1. `static-analysis` — clang-format, cppcheck, clang-tidy, MISRA
2. `dependency-scan` — Dependabot config + pip-audit for Python tooling
3. `build-and-fingerprint` — multi-target build + flash/RAM extraction + thresholds + delta
4. `native-tests` — native host tests + code coverage (gcov → lcov)
5. `unity-tests` — `pio test -e unit-test`
6. `ota-sign-verify` — sign a test firmware + verify the signing round-trip on host
7. `fuzz-test` — lightweight fuzz harness for Art-Net/sACN frame parsing on host
8. `build-artifacts` — upload .bin, .elf, .map, manifest.json, sha256, size report

Rationale: Sequential ordering avoids wasting runner minutes on later stages if early gates fail. Static analysis before build catches issues cheaply. Tests after build ensures we're testing the same binary.

### D2: Static Analysis Toolchain
**Decision:** Add 4 layers of static analysis:
- **clang-format**: Install `clang-format`, create `.clang-format` based on the existing code style (120-char width, Allman brace style, 4-space indent — matching the repo's observed style). Add a format-check step that runs `clang-format --dry-run --Werror` on `*.cpp`, `*.h` in `src/`, `include/`, `test/`.
- **cppcheck**: Already in CI via `pio check`. Enhance: add `--enable=warning,performance,portability,information`, `--inconclusive`, `--suppress=missingIncludeSystem`. Add a standalone cppcheck step that scans `src/` and `include/` directly with MISRA addon if available.
- **clang-tidy**: Install clang-tidy via apt, create `.clang-tidy` config with embedded-relevant checks (bugprone-*, performance-*, readability-*, google-runtime-*, clang-analyzer-*) plus ESP-IDF include paths. Run on a representative subset of sources (or all, if time permits within CI timeout).
- **MISRA C++ 2008**: Use cppcheck with the `misra++` addon (`python3 -m misra` or `cppcheck --addon=misra`). If the addon is unavailable in CI, fall back to cppcheck's built-in MISRA ruleset via `--rule-texts=misra-cpp.txt`. Document that full MISRA requires PC-lint Plus (licensed), but the cppcheck addon provides baseline compliance checking.

### D3: Memory Footprint Monitoring
**Decision:** After each build, parse the `.elf` file using `xtensa-esp32-elf-size` or `esptool.py` to extract:
- Flash usage (text + rodata): `esptool.py --chip esp32s3 flash_size` or `xtensa-esp32-elf-size --format=berkeley .pio/build/<env>/firmware.elf`
- RAM usage (bss + data): from the same size output
- DRAM/IRAM/DRAM/BSS segments from the `.map` file

Thresholds (fail if exceeded):
- Flash: 90% of available (ESP32-S3 with 4MB flash → ~3.6MB)
- DRAM: 85% of 512 KB (ESP32-S3 has 512 KB SRAM)

Delta tracking: Compare against the previous successful run on the same branch (store baseline as an artifact, or compare against `main` branch). Report growth/shrink per segment. Store results in a `memory_report.json` artifact.

### D4: Code Coverage for Native Tests
**Decision:** Add `--coverage` flags to the native test compilation in `test_native.py`. Specifically:
- Modify `COMMON_FLAGS` in `test_native.py` to include `-fprofile-arcs -ftest-coverage` (or `--coverage`)
- After each test runs, the `.gcda` files are generated in the build directory
- Use `lcov` to capture: `lcov --capture --directory build/test_native --output-file coverage.info`
- Generate HTML report: `genhtml coverage.info --output-directory coverage_report`
- Upload report as artifact
- Set a minimum coverage threshold gate (e.g., `--rc lcov_branch_coverage=1 --rc geninfo_auto_faq=1`; fail if total line coverage < 70% — start low, adjust based on baseline)

For Unity tests: PlatformIO's `unit-test` env already uses `build_type = debug`. Coverage for Unity tests on the host can be enabled via `pio test --coverage` or by adding `--coverage` to the `unit-test` env build flags in `platformio.ini`.

### D5: Unity Tests in CI
**Decision:** Add `pio test -e unit-test` as a CI step. The native test job stays as-is; Unity tests run as a parallel job (or sequential after native tests). Use `pio test -e unit-test --verbose` for detailed output.

### D6: OTA Sign/Verify Test
**Decision:** In a dedicated job, test the signing flow end-to-end on the host (no hardware):
1. Generate a test Ed25519 key pair using `tools/gen_ota_keys.py` (or a temp key, not committed)
2. Build firmware for `esp32s3_psram` (or reuse the artifact from the build job)
3. Sign the `.bin` using `tools/sign_ota.py` with the test private key
4. Verify the signing round-trip: compute SHA-256, verify the 64-byte signature matches using `openssl` or `cryptography` in Python
5. Verify the signed image is larger than the unsigned by exactly 64 bytes
6. Clean up the temp key (never committed)

This validates the `tools/sign_ota.py` script and the on-device verification logic's test counterpart. It does NOT replace hardware OTA testing.

### D7: Fuzz Testing
**Decision:** Create a lightweight host-based fuzz harness for the Art-Net/sACN packet parsing logic. Since the firmware's core parsing functions are compiled into native tests, extract the key parse functions into a fuzz target that:
1. Takes a byte buffer as input (via stdin or file)
2. Feeds it to the Art-Net/sACN parser entry point
3. Verifies no crashes (use AddressSanitizer: compile with `-fsanitize=address,fuzzer`)

This runs as an optional CI step (not a hard gate initially). Use `libFuzzer` with clang on Ubuntu. If the project's parser functions are not easily isolatable, create a thin fuzz harness in `test/native/` that includes the relevant headers and calls the parse functions with sanitized inputs.

### D8: Release Artifacts and Signing
**Decision:** In `release.yml`, enhance the release job to:
1. For each build env, compute `sha256sum` of the `.bin` file
2. Write SHA256 hashes to a `SHA256SUMS.txt` file
3. If `OTA_SIGN_ENABLED` is set for the env (currently only `esp32s3_n16r8_eth` defaults to 1), sign the firmware using `tools/sign_ota.py` with a CI secret (`OTA_SIGN_KEY`)
4. Upload both signed and unsigned artifacts
5. Include `.map` and `.elf` files as artifacts (for debugging)
6. Generate a `manifest.json` with build metadata (env, commit SHA, build timestamp, flash/RAM usage)
7. Enhanced release notes: include memory usage table, artifact list, signing status

The `OTA_SIGN_KEY` (Ed25519 private key in PEM format) is stored as a GitHub secret and injected at signing time. The public key is already embedded in `src/net/ota_sign.cpp`.

### D9: Dependency Scanning
**Decision:**
- Add a `.github/dependabot.yml` to track:
  - GitHub Actions versions (updates `actions/checkout@v4`, `actions/setup-python@v5`, etc.)
  - Python dependencies (if a `requirements.txt` is added for CI tooling)
- Add `pip-audit` step in CI to scan Python tool dependencies (e.g., `cryptography` used by `sign_ota.py`)
- For PlatformIO/Arduino library deps: add a step that runs `pio pkg list` and checks for known CVEs using `cve-bin-tool` or a simple version check against the AsyncTCP/ESPAsyncWebServer GitHub releases.
- The git-based deps (`AsyncTCP`, `ESPAsyncWebServer`) will be scanned by Dependabot's `github-actions` ecosystem via a custom update script, or by a manual `curl` check against their GitHub release/latest tags.

## Constraints

1. **Build gate:** Every source change must pass `pio run -e esp32s3_psram` (per project constraint). The CI `build-and-fingerprint` job's `esp32s3_psram` build must succeed before any artifact-dependent steps proceed.
2. **Runners:** All jobs run on `ubuntu-latest` (GitHub-hosted). Windows/macOS builds are not required in CI (platformio builds are cross-compilation on Linux).
3. **No hardware in standard CI:** HIL testing, power profiling, and real OTA verification on-device are out of scope for automated CI. They require a self-hosted runner with USB passthrough + a connected device (documented as optional/manual).
4. **Time limits:** Each job must complete within 60 minutes (GitHub Actions timeout). ESP32-S3 from-source builds are the longest (~10-15 min each); 5 parallel builds may hit runner resource limits. Solution: use `fail-fast: false` on build matrix and accept parallel builds on GitHub-hosted runners.
5. **Secrets:** Only `OTA_SIGN_KEY` is needed as a CI secret for the release workflow. No other secrets required.
6. **No new source code changes required** for most steps — the CI pipeline orchestrates existing tools (`pio`, `python3`, `cppcheck`, `clang-format`, `clang-tidy`, `lcov`, `sign_ota.py`).

## Affected Boundaries

| Boundary | Change |
|---|---|
| `.github/workflows/ci.yml` | Complete rewrite: 8 sequential jobs replacing 3 |
| `.github/workflows/release.yml` | Add SHA256, signing, map/elf artifacts, manifest.json |
| `.github/dependabot.yml` | New file: track GH Actions + pip deps |
| `.clang-format` | New file: code style rules (120 col, Allman, 4-space) |
| `.clang-tidy` | New file: clang-tidy check config + ESP-IDF include paths |
| `test/native/test_native.py` | Add `--coverage` flags to `COMMON_FLAGS`, emit `.gcda` files |
| `platformio.ini` | Add `--coverage` to `[env:unit-test]` build_flags (or `build_type` with coverage flags) |
| `scripts/ci_local.sh` / `scripts/ci_local.ps1` | Update to match new CI stages |
| `.github/scripts/` | New directory: helper scripts (e.g., `extract_sizes.py`, `check_firmware_size.py`) |
| `ci_baseline.json` | New file (gitignored): per-env flash/RAM baseline for delta tracking |

## Data Flow

```
1. PR push / push to main
   ↓
2. static-analysis job
   ├── clang-format --dry-run (format check)
   ├── cppcheck (static analysis)
   ├── clang-tidy (static analysis)
   └── misra (MISRA C++ compliance)
   ↓
3. dependency-scan job
   ├── pip-audit (Python deps)
   └── pio pkg list + CVE check (PlatformIO deps)
   ↓
4. build-and-fingerprint job (matrix: 5 envs)
   ├── pio run -e <env>           # MUST pass for esp32s3_psram
   ├── xtensa-esp32-elf-size → extract Flash/RAM
   ├── check thresholds (Flash ≤ 90%, DRAM ≤ 85%)
   ├── compare against baseline → write delta report
   └── upload: firmware.bin, firmware.elf, firmware.map, memory_report.json
   ↓
5. native-tests job
   ├── generate config_templates.gen.h (via gen_config_templates.py)
   ├── compile 4 tests with --coverage
   ├── run each test → collect .gcda
   ├── lcov → coverage.info → genhtml
   ├── threshold check: coverage ≥ 70%
   └── upload: coverage.info, coverage.html, test logs
   ↓
6. unity-tests job
   ├── pio test -e unit-test
   └── upload: test output logs
   ↓
7. ota-sign-verify job
   ├── generate test Ed25519 key pair (temp, never committed)
   ├── sign firmware.bin via tools/sign_ota.py
   ├── verify SHA-256 + signature round-trip (Python)
   └── upload: signed_firmware.bin, verification log
   ↓
8. fuzz-test job (optional, non-blocking)
   ├── compile fuzz harness with -fsanitize=address,fuzzer
   ├── run libFuzzer for 5 min or N iterations
   └── upload: fuzzer artifacts, crash reproduction cases
```

## Failure Modes & Mitigations

| Failure Mode | Mitigation |
|---|---|
| `clang-format` finds unformatted code | Fail the job; developer must run `clang-format -i` on changed files before merge |
| `cppcheck` reports errors | Fail the job; fix all new errors (existing warnings can be suppressed via inline suppressions) |
| Flash/RAM exceeds threshold | Fail the build; requires optimization to reduce footprint |
| Coverage drops below threshold | Fail; requires additional test coverage before merge |
| Unity tests fail to link | Likely due to `build_src_filter` gap; update `src/test_stubs.cpp` to stub new symbols |
| `pio run` from-source S3 build exceeds 60 min | Use `fail-fast: false`; if consistent, consider runner upgrade to `ubuntu-24.04-xl` (more cores) |
| `ota-sign-verify` fails (wrong key) | Key is generated fresh per run; ensure `cryptography` is installed in CI env |
| Fuzz test finds a crash | Upload reproduction case as artifact; mark job as failed but non-blocking initially |
| MISRA addon unavailable on runner | Use cppcheck's built-in MISRA rules; document that PC-lint Plus is the production-grade option |

## Rollout Plan

**Phase 1 (MVP — immediate):**
- Add `clang-format` check (format enforcement)
- Add Unity tests to CI (`pio test -e unit-test`)
- Add `.github/dependabot.yml` for GitHub Actions versions
- Add pip-audit for Python tool deps
- Fix `test_native.py` to emit coverage flags
- Add coverage collection + threshold to native test job
- Add SHA256 + `.map`/`.elf` artifacts to CI build job
- Add `.clang-format` based on existing code style

**Phase 2 (extended — within 1 month):**
- Add clang-tidy with `.clang-tidy` config
- Add MISRA compliance check (cppcheck MISRA addon)
- Add flash/RAM footprint monitoring + thresholds
- Add memory footprint delta tracking vs baseline
- Enhance `release.yml`: signing, SHA256SUMS, manifest.json
- Update `scripts/ci_local.sh` and `ci_local.ps1` to match new stages

**Phase 3 (advanced — optional, hardware-dependent):**
- Add fuzz testing job (host-based, AddressSanitizer)
- Document HIL testing workflow for self-hosted runner with USB device
- Add power profiling step for self-hosted runner with power analyzer

## Validation

1. **CI must pass on clean `main`:** After implementing, push a no-op commit to `main`; all jobs must pass.
2. **CI must fail on format violation:** Introduce a deliberate `clang-format` violation; the `static-analysis` job must fail.
3. **CI must fail on coverage drop:** Temporarily reduce a test count; the `native-tests` job must fail at the threshold gate.
4. **CI must fail on footprint growth:** If flash exceeds 90% threshold, `build-and-fingerprint` must fail.
5. **Release produces signed firmware:** Tag a release; verify `signed_firmware.bin` is 64 bytes larger than unsigned, SHA256 checksums match, and manifest.json contains correct metadata.
6. **Unity tests run in CI:** Verify `pio test -e unit-test` output appears in the CI job logs.
7. **Local runner parity:** `scripts/ci_local.sh all` must reproduce all CI stages locally.
8. **Dependabot updates are actionable:** Verify Dependabot creates PRs for outdated actions.

## Open Questions

1. **MISRA tooling:** Does the ubuntu-latest runner include cppcheck's MISRA addon (`python3 -m misra`) by default, or must it be installed via pip? → Resolve by testing `misra-gp.py` availability; if unavailable, use `cppcheck --rule-texts` or document PC-lint Plus as the production option and use cppcheck's built-in MISRA rules.

2. **Unity test coverage collection:** Can `pio test -e unit-test` enable gcov coverage natively, or must the `[env:unit-test]` be modified to add `--coverage` build flags? → Resolve by modifying `platformio.ini [env:unit-test]` to add `build_flags = --coverage` and `test_transport = native` if needed.

3. **clang-tidy include paths:** clang-tidy needs to know the ESP-IDF include paths for the ESP32-S3 target. Is there an existing `compile_commands.json` from PlatformIO, or must we generate one via `pio run -E` or `bear -- pio run -e esp32dev`? → Resolve by using `pio run -e esp32dev --target compiledb` (if PlatformIO generates compile_commands.json) or installing `bear` and wrapping the build.

4. **Fuzz harness isolation:** Are the Art-Net/sACN parsing functions (`artnet.cpp`, `sacn.cpp`) easily isolatable for host-based fuzzing with AddressSanitizer, or do they depend on too many ESP-IDF APIs? → Resolve by creating a minimal fuzz harness that includes only header-only pure functions (e.g., packet validation, frame decoding) or stubs the hardware-dependent paths.

5. **HIL runner setup:** Is there a self-hosted runner available with USB device passthrough for HIL testing? → This is a deployment question for the project owner; the plan documents the HIL workflow but does not require it for CI on GitHub-hosted runners.

## Task List (Ordered)

### Phase 1

1. **Create `.clang-format`** — Based on existing code style: 120-column limit, Allman brace style, 4-space indent, pointer alignment left, namespace indentation. Reference: existing `src/*.cpp` files.

2. **Add clang-format check to CI** — New step in `static-analysis` job: install `clang-format`, run `clang-format --dry-run --Werror` on all `*.cpp` and `*.h` in `src/`, `include/`, `test/`.

3. **Add clang-tidy job** — Install `clang-tidy`, create `.clang-tidy` config with embedded-relevant checks, generate `compile_commands.json` via `pio run` or `bear`, run `clang-tidy -p .` on `src/` files.

4. **Add MISRA compliance check** — Install `misra-gp` or use cppcheck's MISRA rules, run on `src/` and `include/`.

5. **Add Dependabot config** — Create `.github/dependabot.yml` tracking `github-actions` ecosystem for workflow files and `pip` for Python tool deps.

6. **Add pip-audit step** — In CI, install `pip-audit`, run `pip-audit` against the Python dependencies used by CI scripts.

7. **Add Unity tests to CI** — New job: install PlatformIO, `pio test -e unit-test`, upload logs.

8. **Enable code coverage for native tests** — Modify `test/native/test_native.py` `COMMON_FLAGS` to include `--coverage`; add `lcov` step to capture + report; set threshold gate (70% line coverage).

9. **Add PlatformIO dependency check** — Step that runs `pio pkg list` and outputs a dependency manifest; optionally compare against a known-good `dependencies.lock`.

10. **Add memory footprint monitoring** — Script `scripts/extract_sizes.py` that parses `.elf` or `.map` for Flash/RAM usage; add threshold checks (Flash ≤ 90%, DRAM ≤ 85%).

11. **Add memory delta tracking** — Compare current build sizes against baseline stored in artifact; report per-segment delta.

12. **Add SHA256 + map/elf artifacts to CI build job** — Compute `sha256sum` of firmware.bin, upload `.map` and `.elf` alongside `.bin`.

13. **Create `manifest.json` generator** — Script `scripts/gen_manifest.py` that captures build metadata (env, commit, timestamp, flash/RAM, sha256).

14. **Update `scripts/ci_local.sh` and `ci_local.ps1`** — Add new stages matching CI jobs.

15. **Format all source with clang-format** — Run `clang-format -i` on all files to establish a clean baseline (this is a code change, must be coordinated).

### Phase 2

16. **Enhance `release.yml`** — Add: SHA256SUMS.txt generation, firmware signing via `tools/sign_ota.py` with `OTA_SIGN_KEY` secret for `esp32s3_n16r8_eth` env, manifest.json, `.map`/`.elf` artifacts, enhanced release notes with memory usage table.

17. **Add fuzz test harness** — Create `test/native/fuzz_artnet.cpp` or `fuzz_sacn.cpp` that compiles with `-fsanitize=address,fuzzer`; add `fuzz-test` CI job.

18. **Document HIL testing workflow** — Create `docs/ci/hil-testing.md` describing self-hosted runner setup, device connection, and test procedure.

### Phase 3 (if required)

19. **Add hardware-in-loop job** — Optional job triggered on `workflow_dispatch` or via self-hosted runner label, requiring a connected ESP32-S3 + test rig.

20. **Add power profiling step** — Connected power analyzer on self-hosted runner; measure sleep current and active current; compare against spec.
