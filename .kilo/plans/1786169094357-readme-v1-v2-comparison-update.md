# README V1→V2 Comparison Update

## Goal
Expand the "Migration Guide: V1 → V2" section in `README.md` with a comprehensive, technically-grounded side-by-side comparison between V1 (monolithic `main.cpp`) and V2 (5-layer modular architecture). The code has been inspected to verify accuracy of every claim.

## Context from code inspection

### V1 architecture (verified from fetched `tombueng/LuxDMX` master branch)
- Single ~5,000-line `main.cpp` containing: pin `#define`s, inline `Preferences` NVS load/save, `ArtnetWifi` library for Art-Net/sACN, `esp_dmx` for DMX UART output, inline WiFi/Ethernet, inline AsyncWebServer + WebSocket, inline RDM, inline serial console, inline FreeRTOS task creation
- `platformio.ini` dependencies: `rstephan/ArtnetWifi`, `adafruit/Adafruit NeoPixel`, `adafruit/Adafruit GFX Library`, `adafruit/Adafruit SSD1306`, `adafruit/Adafruit SH110X`, `adafruit/Adafruit SSD1351 library`, `ESP32Async/AsyncTCP`, `ESP32Async/ESPAsyncWebServer`
- `MAX_OUTPUTS = 2`, UART-based DMX (UART0 = console, UART1/UART2 = DMX out)
- Config defaults via `-DDEF_*` macros; no separate template/config-schema system
- NVS keys: `o0_*`, `o1_*` for outputs; `apfb` (bool) for WiFi-AP-on-wired-failure
- `SOURCE_TIMEOUT_MS = 4000`
- Fixed 40 fps free-run, no per-output rate or delta mode
- `ArtSync` staging not implemented (frames route immediately)
- No Ed25519 OTA signing
- No soak-test monitor

### V2 architecture (verified from local source files)
- `main.cpp` is ~130 lines — thin wiring: `nvs_migrate::migrateNvsKeys()` → `cfgcore::load()` → `outputInitAll()` → `startSacn()` → `webRegisterRoutes()` → `createTasks()`
- 5-layer split: `drv/` (RMT TX, UART RX, GPIO DE/RE), `cfg/` (schema table, config_core, nvs_migrate, serial console), `core/` (dmx_buffer seqlock, merge_engine, sender_tracker, frame_router, rdm_engine, rdm_disc, output_init, stats), `net/` (artnet.cpp, sacn.cpp, artnet_bridge.cpp, websocket.cpp, ws_frame.cpp, ws_handler.cpp, web_server.cpp, web_routes.cpp, web_pages.cpp, ethernet.cpp, net_state.cpp, ota.cpp, ota_sign.cpp, setup_portal.cpp), `sys/` (tasks.cpp, led_status.cpp, display.cpp, soak_monitor.cpp, sys_platform.cpp, firmware_version.cpp)
- `include/` headers: `config_schema.h`, `config_enums.h`, `config_types.h`, `output.h`, `rdm_types.h`, `seqlock.h`, `eth_phy.h`
- `templates/*.ini` (3 files: `_base.ini`, `luxdmx_4uni.ini`, `luxdmx_v6.ini`) — schema-driven, selected by `-DDEFAULT_TEMPLATE=...`
- RMT-based DMX TX (`src/drv/dmx_rmt.h:13` — "immune to core-0 network DMA contention")
- UART RX-only for RDM responses (`src/drv/uart_rx.h`)
- `MAX_OUTPUTS = 4`, RMT channels 0-3 (S3 ch3 has DMA, others ISR-refill)
- Schema field table in `src/cfg/config_schema.cpp` (`CONFIG_FIELDS[]` + `OUTPUT_FIELDS[]`)
- `nvs_migrate.cpp:13` — `migrateNvsKeys()` migrates `o0_*`→`a_*`, `o1_*`→`b_*`, `apfb`→`fbmode`
- Seqlock in `include/seqlock.h` — single-writer/single-reader, 8 retries, torn reads skipped
- Crash-guard in `src/sys/tasks.cpp:42` — `dmxInitGuardBegin()`/`dmxInitGuardEnd()` with NVS `dmxcrash` counter
- Core pinning: `dmxTxTask` on core 1 prio 19, `netRxTask` on core 0 prio 5
- `TXSTYLE_DELTA` / `TXSTYLE_CONTINUOUS` per-output transmit style
- `TXSRC_LOCAL` / `TXSRC_ARTNET` style-source tracking
- 5 TX rate options: 25/24/30/40/50 ms
- ArtSync staging: `dmxStaged[]` + `artSyncMode` flag, `commitArtSyncStaged()` on sync or timeout
- sACN Stream Sync: `sacnStaged[]` + 500ms commit grace, 2500ms loss timeout
- sACN Universe Discovery: consume/discard frame vector 0x00000004
- Ed25519 OTA signing framework in `src/net/ota_sign.cpp` (`OTA_SIGN_ENABLED` for prod)
- Soak monitor in `src/sys/soak_monitor.cpp` (`LUXDMX_SOAK_TEST`, 30KB DRAM threshold, `/diag/soak-stats`)
- Extended RDM PIDs: DEVICE_MODE, DEVICE_MODES, IDENTIFY_MODE, BURN_IN, DEVICE_HOURS, DEVICE_POWER, PERSONALITY_DESCRIPTION, SENSOR_RECORD (in `src/core/rdm_typed.cpp`)
- Sub-device enumeration: `rdmSubDeviceCount()` in `rdm_typed.cpp:131`
- Background queue policy: `g_bqPolicy` (0-4, disabled by default)
- `linkLossMode` enum: 0=retry, 1=AP, 2=reboot, 3=join WiFi (was `apFallback` bool)
- AsyncTCP pinned to core 0, 16KB stack, 128 queue size

## Change to make

Replace the existing "Migration Guide: V1 → V2" section (README.md lines 565-587) with a comprehensively expanded version covering:

### 1. Architecture: Monolith → 5-layer modular
- Table: V1 `main.cpp` → V2 `drv/`+`cfg/`+`core/`+`net/`+`sys/`, with file references
- Note: `main.cpp` is now ~130 lines of thin wiring

### 2. DMX Transmission: UART → RMT
- Table: UART+GPTimer ISR (esp_dmx) → RMT hardware TX + RX-only UART for RDM
- Issue #64 (core-0 network DMA contention corrupting breaks) — cite `dmx_rmt.h:2-9`
- RDM transport: UART half-duplex switch → RMT-TX + separate RX-only UART, never released mid-frame

### 3. Outputs: 2 → 4 universes
- Table: 2 (UART1/UART2) → 4 (RMT ch 0-3; only ch3 has DMA on S3 per `dmx_rmt.h:101`)
- Output C/D are DMX-only; A/B are RDM-capable
- `output_mode_t` enum: DMX-only vs RDM-full (vs V1's implicit pin-based selection)

### 4. Configuration: Macros → Schema-driven templates
- Table: `-DDEF_*` macros → `templates/*.ini` (selected by `-DDEFAULT_TEMPLATE=...`)
- Single field table in `config_schema.cpp` drives NVS, serial console, web form, native test
- NVS key migration: `o0_*`/`o1_*` → `a_*`/`b_*` (cited from `nvs_migrate.cpp:10-11`)
- `apfb` (bool) → `fbmode` (enum: 4 policies)
- `linkLossMode` never opens an unsecured AP (requires password)

### 5. Network Stack
- Table: `ArtnetWifi` library → self-implemented Art-Net (`src/net/artnet.cpp`) + sACN (`src/net/sacn.cpp`)
- Library deps: ArtnetWifi + 4×Adafruit → only AsyncTCP + ESPAsyncWebServer
- AsyncTCP pinned to core 0 with 16KB stack + 128 queue (`platformio.ini:43-47`)
- W5500/RMII runtime-selectable (was build-time)

### 6. Task Scheduling & Core Affinity
- Table: `loop()` on core 0 → dedicated `dmxTxTask` (core 1, prio 19) + `netRxTask` (core 0, prio 5)
- RDM serviced every 1ms tick (was per-frame)
- Dedicated `ledTask`, `displayTask`, `versionCheckTask`

### 7. RDM
- Table: basic PIDs → extended PIDs (DEVICE_MODE, BURN_IN, DEVICE_HOURS, etc.)
- Sub-device enumeration (was not implemented)
- RDM transport change (see #2)

### 8. Transmit Style & Output Rate
- Table: fixed 40 fps → per-output rate (20/25/33.3/40/41.7 fps) + Continuous/Delta
- `txStyleSrc` tracks local vs controller-set style
- Per-output HTP/LTP merge (same as V1 but now in dedicated `merge_engine.cpp`)

### 9. Web UI & Config Lifecycle
- Table: inline HTML → `src/pages/*.html` + `extra_scripts.py` → `src/generated/*.h`
- Config apply: all-reboot → live for most settings (cite `config_schema.cpp` CFG_LIVE vs CFG_REBOOT flags)
- OTA: httpUpdate → +GitHub release + URL install + Ed25519 signing
- Config import/export REST endpoints

### 10. Crash Safety
- Table: inline `setup()` init → guarded init (`dmxInitGuardBegin`/`dmxInitGuardEnd` in `tasks.cpp:42`)
- OTA rollback counter (`ota.cpp:23`, `OTA_BOOT_TRIES = 3`)

### 11. Breaking Changes (expanded from current)
- esp_dmx removed → first-party `dmx_rmt.h` + `rdm_types.h` drop-in
- platform pinned to pioarduino v55.03.39
- S3 from-source + brownout disable
- ENC28J60 unsupported
- ArtnetWifi removed
- Adafruit libraries removed
- MAX_OUTPUTS 2→4
- apFallback bool → linkLossMode enum (with AP password requirement)

### 12. Migration Path
- Expand: `nvs_migrate::migrateNvsKeys()` called at `main.cpp:41`
- Inline fallback in `cfgcore::load()` at `config_core.cpp:164`
- Crash-guard counter behavior

## Validation
- All file:line references verified against the V2 source tree (`src/`, `include/`, `templates/`)
- V1 architecture verified from fetched `tombueng/LuxDMX` master `main.cpp` + `platformio.ini` + `README.md`
- No source code changes — README.md content update only

## Open questions
- None — this is a documentation-only update