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


The report uses the following stable top-level fields:

| Field | Meaning |
|---|---|
| `schema` | Versioned report identifier, currently `luxdmx.firmware-artifact.v1` |
| `status` | `pass` or `fail`; required outputs are validated before `pass` |
| `environment` | Exact PlatformIO environment name |
| `board` | PlatformIO board identifier |
| `profile` | `development` or `release` |
| `commit` | Source commit used for the build |
| `platform` | Platform identifier reported by PlatformIO metadata |
| `framework` | ESP-IDF/framework identifier reported by PlatformIO metadata |
| `platformio_version` | PlatformIO Core version used by the reporter |
| `python_version` | Python runtime version used by the reporter |
| `artifacts` | Map of required and diagnostic files to path/presence/bytes/SHA-256 records |
| `errors` | Deterministic failure messages; empty for a passing report |

The `artifacts` map contains `firmware.bin`, `partitions.bin`, and `bootloader.bin` as required entries and `firmware.elf` as an optional diagnostic entry. Byte sizes and SHA-256 values are nested under each artifact record; this is the canonical schema rather than separate top-level `firmware_bytes` fields. The report uses stable key ordering and UTF-8 text/JSON so it can be diffed and consumed by later packaging and hosted-flasher work. It must not silently compare sizes across different partition layouts or board profiles.

## 5. Current CI boundary and validation evidence

The current workflow builds the three development environments in its firmware matrix and, after each build, runs `tools/firmware_artifact_report.py` and uploads `firmware-metadata-<env>`. The six-environment PlatformIO manifest defines three development profiles and three signing-flag release profiles. On commit `a355d252ac9bd82a1211c44b565bfb7a54af2462`, all six profiles were also built sequentially in the local validation run and produced passing per-environment reports. Release profiles are therefore locally exercised, but they are not claimed as part of the current PR development matrix; tag/release CI policy remains a separate release gate.

M0.4.3 does not prove flash-size correctness, PHY behavior, OTA acceptance, signing-key provisioning or HIL hardware behavior. A local PlatformIO warning or successful compile is evidence for that invocation only; it is not a hardware validation result.

## 6. Validation snapshot

The following snapshot records the M0.4.3.4 local validation at commit `a355d252ac9bd82a1211c44b565bfb7a54af2462`. Each row passed `pio run -e <env>` followed by the bounded reporter; sizes are bytes for `firmware.bin`.

| Environment | Profile | Board | Status | firmware.bin bytes |
|---|---|---|---|---:|
| `esp32dev` | development | `esp32dev` | `pass` | 903216 |
| `wt32eth01` | development | `wt32-eth01` | `pass` | 902688 |
| `esp32s3_psram` | development | `esp32-s3-devkitc-1` | `pass` | 911200 |
| `esp32dev_release` | release | `esp32dev` | `pass` | 903648 |
| `wt32eth01_release` | release | `wt32-eth01` | `pass` | 903648 |
| `esp32s3_psram_release` | release | `esp32-s3-devkitc-1` | `pass` | 911616 |
