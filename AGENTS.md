# AGENTS.md

LuxDMX-v2 — Art-Net/sACN → DMX512 gateway firmware for ESP32 (ESP32, WT32-ETH01, ESP32-S3+PSRAM). Pure ESP-IDF v5.2 rewrite via PlatformIO; C implementation with a single C++ entrypoint. **Work on branch `idf-only`.**

## Build

PlatformIO + ESP-IDF (`framework = espidf`); envs are in `platformio.ini`.

- `pio run -e esp32s3_psram` — canonical target (per `docs/SYSTEM_SPECIFICATION/INDEX.md` Build Gate)
- `pio run -e esp32dev` / `pio run -e wt32eth01` — other firmware targets
- upload/monitor: `pio run -e esp32dev -t upload` (esptool, COM3) / `pio device monitor` (115200)
- `native` env is a host stub; no tests are wired to it (there are no test sources anywhere)

⚠️ **The tree does not currently build.** `components/lux_web/CMakeLists.txt` lists `src/web_websocket.c` and `src/web_assets.c`, and `web_server.c`/`wifi_manager.c` include headers (`web_websocket.h`, `wifi_events.h`, `wifi_config.h`, `captive_portal.h`) that don't exist on disk. `.pio/build/*` contains only failed-build state. Also, no platform/toolchain packages are installed under `~/.platformio`, so a first `pio run` downloads gigabytes.

## Architecture (current reality)

- `main/main.cpp` (`app_main`) — the only C++ file, everything else is C. Boot order: logger → NVS → `hw_init` → LED → `config_engine_init`/`config_load` → `wifi_manager_init` → `web_server_init/start`. Note it hardcodes placeholder creds `wifi_sta_connect("SSID", "PASSWORD")`.
- Components under `components/`: `lux_common`, `lux_hw`, `lux_led`, `lux_log`, `lux_config`, `lux_wifi`, `lux_web`, `lux_test` (empty scaffold). Only Phase-1 scope exists: WiFi + LED + schema-driven config + web server.
- Config is schema-driven: the field table in `components/lux_config/src/config_schema.c` drives NVS, JSON, and serial console. Board defaults are the hardcoded `board_templates[]` table in that same file — **not** the `templates/*.ini` files.

## Docs vs. code — read these first, trust these second

`codebase_index.md` and `docs/SYSTEM_SPECIFICATION/` describe a *target* 5-layer architecture (RDM/DMX core, merge engine, seqlock, OTA + Ed25519, Ethernet/W5500/RMII, 6 build envs, build-time PROGMEM/template generators, Kconfig, tests, CI). None of that exists in the current code: `platformio.ini` has no build hooks/codegen, no Kconfig, no `tools/`, `test/`, or `.github/` on this branch, and components are only Phase 1. Treat the docs as the roadmap/spec; `platformio.ini` + code are ground truth. Do not write code that assumes documented subsystems already exist.

## Other gotchas

- `templates/*.ini` are orphaned data — their keys (`wifissid`, `a_tx`, …) don't match the live C schema keys (`wifi_ssid`, …) and no generator consumes them.
- Root `CMakeLists.txt`, `sdkconfig*`, `dependencies.lock`, `managed_components/` are PlatformIO-generated and gitignored; don't hand-edit them. `partitions.csv` (dual-slot OTA) is tracked.
- `origin/main` holds a different, legacy Arduino codebase (pioarduino + AsyncWebServer, ~295 files incl. CI/tests). Don't copy artifacts from it into `idf-only`.