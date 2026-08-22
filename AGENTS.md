# AGENTS.md

LuxDMX-v2 — Art-Net/sACN → DMX512 gateway firmware for ESP32, WT32-ETH01 and ESP32-S3 + PSRAM. The repository uses PlatformIO with ESP-IDF and a C implementation with a single C++ entrypoint in `main/main.cpp`. The current integration branch is `main`; feature work follows `docs/DEVELOPMENT_PLAN.md` and the issue dependency order in issue #16.

## Build and validation

PlatformIO environments are defined in `platformio.ini`:

- `pio run -e esp32s3_psram` — canonical WiFi/PSRAM target.
- `pio run -e esp32dev` — ESP32 development target.
- `pio run -e wt32eth01` — WT32-ETH01 Ethernet development target.
- `pio run -e esp32s3_psram_release`, `pio run -e esp32dev_release`, and `pio run -e wt32eth01_release` — signing-enforced release profiles.
- `python3 tools/native_run.py --clean` — clean native CTest suite.
- `python3 tools/repository_hygiene.py` — tracked/workspace generated-file and secret hygiene check.
- `python3 tools/test_repository_hygiene.py` — host violation tests for the hygiene checker.
- `python3 tools/test_ci_capture_context.py` — safe CI diagnostics collector test.
- `python3 tools/test_ota_sign.py` — host signed-image contract test.

A normal change must preserve the repository-hygiene gate, native gate and the three development firmware builds. Changes to CMake source lists, Kconfig/defaults, partition layout, timing-critical drivers, OTA, or task topology require a clean build of the affected environments. Hardware behavior is not considered verified by a native shim alone.

## Architecture

`main/main.cpp` is lifecycle wiring: logger → NVS → OTA recovery guard → hardware/LED → configuration → DMX/RDM tasks → protocol/network → web server. Runtime logic belongs in components. `lux_common` owns shared headers and board contracts; `lux_config` owns schema/NVS/migration; `lux_core` owns DMX buffers, routing, merge, scenes, sender tracking and RDM logic; `lux_drv` owns RMT/UART/GPIO/display/LED drivers; `lux_net` owns protocol, HTTP/WebSocket, Ethernet state and OTA; `lux_sys` owns task creation, crash guard, logging, alerts and soak monitoring; `lux_web` owns REST/UI/WebSocket presentation; `lux_test` and `test/` own host shims and tests.

Core-affinity and timing are contractual. Network/web work runs on core 0; DMX/RDM timing-critical work runs on core 1. The DMX buffer is seqlock-protected, the DMX frame is 513 bytes, queues and payloads are bounded, and timing constants must be traced to the relevant system specification before modification.

## Documentation and implementation truth

`docs/SYSTEM_SPECIFICATION/` and `docs/REFACTOR_PLAN.md` define target behavior, protocol layouts, timing, configuration semantics and safety contracts. `platformio.ini`, source code and generated build output define what is actually present. `docs/DEVELOPMENT_PLAN.md` defines phase gates and Definition of Done. `codebase_index.md` and `Lessons_Learned.md` must be updated in the same logical commit as any behavior change.

Do not implement from a header, config field, UI control or stub alone. A capability is complete only when it has a runtime consumer, negative/error tests, documentation and hardware evidence where it touches a peripheral, waveform, network link or reboot lifecycle.

## Generated files and configuration

Do not hand-edit `.pio/` output, managed components, `dependencies.lock`, root generated CMake state, or generated `sdkconfig.*` files. Use tracked `sdkconfig.defaults` and PlatformIO/Kconfig inputs for reproducible defaults. `partitions.csv` is a tracked dual-slot OTA contract and may be changed only with explicit partition-size, bootloader and OTA migration review.

## Development rules

Implement one logical work package at a time. Before coding, record its spec anchors, ownership, state/data model, timing, memory bound, error matrix, security impact and test plan. Add or update tests with the behavior, run the smallest relevant validation first, then the required build matrix. Use focused conventional commits; no WIP commits on `main`.

Never commit secrets or private signing keys. CI diagnostics must use an allowlist and must never serialize the process environment. Development OTA profiles may bypass signing only through an explicit flag; release profiles must fail closed and use protected key injection. Keep PCB schematic, layout, BOM, Gerber, fabrication and electrical-design work outside this firmware roadmap unless a separate user-approved issue changes the boundary.
