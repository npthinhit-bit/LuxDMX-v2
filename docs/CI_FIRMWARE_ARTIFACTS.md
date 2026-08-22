# Firmware matrix and artifact contract

This document defines the build matrix and artifact names used by LuxDMX-v2 CI. It is the source contract for M0.4.3 size/metadata reporting; it does not itself claim that every matrix entry has passed a current build.

## 1. Matrix

| Class | Environment | Board/profile | OTA signing flag | CI role |
|---|---|---|---:|---|
| Development | `esp32dev` | ESP32 DevKit / WiFi | `0` | required PR and `main` firmware gate |
| Development | `wt32eth01` | WT32-ETH01 / RMII | `0` | required PR and `main` firmware gate |
| Development | `esp32s3_psram` | ESP32-S3-WROOM-2 N16R8 / PSRAM | `0` | required PR and `main` firmware gate |
| Release | `esp32dev_release` | ESP32 DevKit / WiFi | `1` | tag/release build gate |
| Release | `wt32eth01_release` | WT32-ETH01 / RMII | `1` | tag/release build gate |
| Release | `esp32s3_psram_release` | ESP32-S3-WROOM-2 N16R8 / PSRAM | `1` | tag/release build gate |

The native environment is a test environment, not a firmware matrix entry. It is validated by `python3 tools/native_run.py --clean` and has its own diagnostics namespace.

## 2. Required output contract

For each firmware environment `<env>`, PlatformIO writes build output under `.pio/build/<env>/`. The CI artifact named `firmware-<env>` must contain exactly the downstream firmware inputs that are present and required by the packaging contract:

```text
.pio/build/<env>/firmware.bin
.pio/build/<env>/partitions.bin
.pio/build/<env>/bootloader.bin
```

`firmware.elf` and map/size intermediates are diagnostics, not release firmware inputs. They belong under `diagnostics-firmware-<env>` or a future size-report artifact. A missing required `.bin` file is a hard artifact failure; it must not be converted into a successful job by `continue-on-error` or a permissive upload pattern.

## 3. Naming and isolation

Artifact names must include the complete PlatformIO environment name. Development and release artifacts must never share a name. Diagnostic artifacts use the separate `diagnostics-<job-or-environment>` namespace defined in `docs/CI_ARTIFACTS.md`.

Every matrix entry must emit a machine-readable report containing at least the environment, board/profile, commit SHA, build result, output paths, byte sizes, SHA-256 values, and toolchain/framework identifiers. The report must not contain secrets or a full process environment dump. `tools/firmware_artifact_report.py` is the canonical dependency-free reporter for this contract.

## 4. Size report contract

`tools/firmware_artifact_report.py` implements this contract for one environment at a time. It resolves `extends` in `platformio.ini`, hashes files in bounded 1 MiB chunks, enforces a 64 MiB per-file safety bound, and exits non-zero when a required artifact is missing or unsafe. Its JSON output uses stable insertion order and UTF-8 encoding.


The size report for `<env>` must record:

| Field | Meaning |
|---|---|
| `environment` | Exact PlatformIO environment name |
| `board` | PlatformIO board identifier |
| `profile` | `development` or `release` |
| `commit` | Source commit used for the build |
| `firmware_bytes` | Size of `firmware.bin` |
| `partitions_bytes` | Size of `partitions.bin` |
| `bootloader_bytes` | Size of `bootloader.bin` |
| `elf_bytes` | Size of `firmware.elf`, when retained as diagnostic |
| `firmware_sha256` | SHA-256 of `firmware.bin` |
| `partition_sha256` | SHA-256 of `partitions.bin` |
| `bootloader_sha256` | SHA-256 of `bootloader.bin` |
| `framework` | ESP-IDF/framework version reported by the build |
| `status` | `pass` or `fail`; never inferred from file existence alone |

The report must use stable key ordering and UTF-8 text/JSON so it can be diffed and consumed by later packaging and hosted-flasher work. It must not silently compare sizes across different partition layouts or board profiles.

## 5. Current gap and next implementation boundary

The current workflow builds only the three development environments in its firmware matrix. The six-environment PlatformIO manifest already defines three release profiles, but release-profile matrix execution and per-environment size/metadata reports are still the next M0.4.3 implementation work. This distinction must remain visible in `codebase_index.md` and release documentation.

M0.4.3 does not prove flash-size correctness, PHY behavior, OTA acceptance, signing-key provisioning or HIL hardware behavior. A local PlatformIO warning or successful compile is evidence for that invocation only; it is not a hardware validation result.
