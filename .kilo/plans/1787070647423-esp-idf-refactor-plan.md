# ESP-IDF Pure Framework Migration Plan
## Refactoring LuxDMX-v2 from Arduino-ESP32 to Pure ESP-IDF

---

## 0. Assessment: Documentation Completeness for Pure-ESP-IDF Rebuild

### Sources Consulted (all from `docs/`)

| Document Family | Files | Status |
|---|---|---|
| System Specifications | 47 specs in `docs/SYSTEM_SPECIFICATION/*.md` | Complete |
| Technical Reference | 30+ files in `docs/TECHNICAL_REFERENCE/*.md` | Complete |
| Lessons Learned | `docs/lessons_learned.md` | Complete (24 BUG entries) |
| Codebase Index | `docs/codebase_index.json` | Complete (file/function/variable map) |
| Build System Spec | Spec #46 + tech-ref | Complete |
| Board Templates | `templates/*.ini` (5 files) | **Partially complete** |
| OTA Key Management | `docs/ota-key-management.md` | Present |

### Can the System Be Rebuilt from Docs Alone?

**Verdict: Partially.** The system specifications are comprehensive — they describe every module's interface, state machine, data flow, configuration integration, error handling, timing constraints, memory model, and safety considerations as black-box contracts. However, several areas have insufficient detail for a pure-ESP-IDF rebuild:

### Critical Gaps Identified

| Gap ID | Area | Specification Coverage | Remediation Needed |
|---|---|---|---|
| G1 | **Board templates** | `esp32dev.ini`, `wt32eth01.ini` missing from `templates/` | Must create templates for all 3 target boards |
| G2 | **ESP-IDF component model** | No spec covers ESP-IDF components vs. Arduino modules | Must define `components/` layout |
| G3 | **Kconfig for multi-board** | Specs describe Arduino `build_flags` but not ESP-IDF Kconfig | Must define `Kconfig` hierarchy |
| G4 | **ESP-IDF WiFi API** | Specs reference `WiFi.begin()`, `WiFiEvent`, `DNSServer` — all Arduino APIs | Must map to `esp_wifi` / `esp_event` / ESP-IDF DNS |
| G5 | **ESP-IDF HTTP server** | Specs reference `AsyncWebServer` (Arduino library dependency) | Must map to `esp_http_server` or retain AsyncTCP shim |
| G6 | **RMT peripheral API** | Specs describe RMT usage abstractly | Must map to `driver/rmt` ESP-IDF functions |
| G7 | **NVS via ESP-IDF** | Specs reference `Preferences` (Arduino wrapper) | Must map to `nvs_flash` direct API |
| G8 | **LED PWM (LEDC)** | Specs describe PWM concept | Must map to `driver/ledc` ESP-IDF |
| G9 | **Partition table** | Specs reference `min_spiffs.csv` without values | Must define ESP-IDF `partitions.csv` |
| G10 | **Setup portal endpoints** | Spec #33 Open Questions: form field names and POST handler unknown | Must define exact API contract |

### Remediation Strategy

Phase 0 is dedicated to gap remediation and proof-of-concept, ensuring documentation sufficiency before incremental migration.

---

## 1. Target Hardware Pin/Peripheral Mapping Table

All pins verified against `templates/*.ini`, `platformio.ini`, and system specs.

| Feature | ESP32-S3-WROOM-2 N16R8 | WT32-ETH01 | ESP32-DevKit (esp32dev) |
|---|---|---|---|
| **MCU** | ESP32-S3-WROOM-2 (N16R8) | ESP32 (WROOM-32) | ESP32 (WROOM-32) |
| **LED** | GPIO48 WS2812 RGB (ledtype=2) | GPIO2 plain (ledtype=1) | GPIO2 plain (ledtype=1) |
| **5-LED panel** | Yes: R=1, G=2, Y=6, B=7, W=15 | No | No |
| **Status LED default** | GPIO48 (WS2812) | GPIO2 (plain) | GPIO2 (plain) |
| **DMX Out A (TX)** | GPIO17 (RMT ch0) | GPIO4 (RMT) | GPIO17 (RMT ch0) |
| **DMX Out A (RX)** | GPIO18 (UART1) | GPIO5 (UART) | GPIO16 (UART) |
| **DMX De/Re A** | GPIO8 (RTS) | — | — |
| **DMX Out B (TX)** | GPIO16 (RMT ch1) | — | — |
| **DMX Out C (TX)** | GPIO5 (RMT ch2, DMX-only) | — | — |
| **DMX Out D (TX)** | GPIO6 (RMT ch3, DMX-only) | — | — |
| **Ethernet** | W5500 SPI: CS=10, SCK=12, MOSI=11, MISO=13, INT=9, RST=3 | RMII LAN8720: MDIO=3, MDC=23, PWR=16, CLK=0 | W5500 SPI optional: CS=5, SCK=18, MOSI=23, MISO=19, INT=4, RST=25 |
| **WiFi** | Station (default) | Station or RMII | Station or W5500 SPI |
| **Display I2C** | SDA=4, SCL=5 | — | — |
| **BOOT button** | GPIO0 (hold 3s to trigger portal) | GPIO0 | GPIO0 |
| **PSRAM** | 8MB octal (SPIRAM) | None | None |

> Source: `templates/luxdmx_4uni.ini`, `templates/luxdmx_v6.ini`, `templates/_base.ini`, `platformio.ini` lines 71, 102-103

---

## 2. Phased Roadmap

### Phase 0: Documentation Remediation & ESP-IDF POC (Prerequisite)

**Goal:** Close documentation gaps, prove ESP-IDF feasibility on all 3 target boards, establish new component structure and Kconfig layout.

| Task | Description | Exit Criteria |
|---|---|---|
| 0.1 | Create missing board templates | `templates/esp32dev.ini` and `templates/wt32eth01.ini` exist with verified pin maps |
| 0.2 | Document ESP-IDF component structure | Plan includes ESP-IDF `components/` layout |
| 0.3 | Define Kconfig hierarchy | `Kconfig` root + per-component `Kconfig` files for all 3 boards |
| 0.4 | Define partition table | `partitions_espidf.csv` with OTA dual-slot + SPIFFS/NVS |
| 0.5 | ESP-IDF WiFi POC | `idf.py build` compiles and boots; WiFi connects or falls back to AP |
| 0.6 | ESP-IDF RMT POC | RMT peripheral transmits a DMX-like waveform on GPIO17 |
| 0.7 | ESP-IDF NVS POC | NVS read/write round-trip succeeds on all 3 boards |
| 0.8 | ESP-IDF HTTP server POC | `esp_http_server` serves a static page on port 80 |
| 0.9 | Document setup portal API contract | This plan includes API endpoints, event schema, form field names |

### Phase 1: Minimum Viable Baseline (WiFi + Status LED + Captive Portal)

**Goal:** Ship a working firmware on all 3 target boards with structured logging, WiFi station connection, captive portal fallback, and GPIO-based status LED. No DMX, no web UI, no protocols beyond HTTP for the portal.

| Task | Description | Commit | Test |
|---|---|---|---|
| 1.1 | ESP-IDF component scaffold | Create `components/` layout with CMakeLists + Kconfig | Host compile of each component |
| 1.2 | Logging infrastructure | `esp_log` wrapper with structured format, serial output | Unit test: log format strings |
| 1.3 | Board detection & pin mapping | Kconfig + runtime board ID resolution | Host test: board pin lookup |
| 1.4 | Status LED driver (ESP-IDF LEDC) | GPIO2/48 plain PWM, WS2812 via RMT, 5-LED panel GPIO | Hardware test: LED on 3 boards |
| 1.5 | NVS credentials storage | `nvs_flash` init + WiFi cred read/write | NVS round-trip test |
| 1.6 | WiFi station connection | `esp_wifi_init` + connect + event handler | Hardware test: connects to MSI/12345678 |
| 1.7 | Boot button detection | GPIO0 hold 3s to set portal flag | Hardware test: BOOT hold triggers portal |
| 1.8 | SoftAP + captive DNS | `esp_netif_create_default_wifi_ap` + ESP-IDF DNS | Hardware test: 192.168.4.1 resolves |
| 1.9 | Setup portal HTTP endpoint | `esp_http_server` serving minimal setup form | Hardware test: browser shows form |
| 1.10 | Setup portal credential POST | Parse form, persist to NVS, reboot | Hardware test: credentials persist |

**Exit Criteria:** All 3 boards boot, status LED animates during WiFi connect, captive portal activates on no-WiFi-creds or BOOT-hold, credentials persist across reboot, serial logs show connection status.

### Phase 2: Art-Net/sACN Core Protocol Stack

**Goal:** Receive Art-Net and sACN on WiFi/Ethernet, route frames to DMX output buffers with seqlock-protected cross-core buffer handoff.

| Task | Description | Commit | Test |
|---|---|---|---|
| 2.1 | UDP socket layer (ESP-IDF lwIP) | `esp_wifi` + lwIP UDP socket abstraction | Host test: socket bind/send/recv |
| 2.2 | Art-Net protocol parser | ArtDMX/Opcodes per spec #17 | Host test: parse ArtDMX frame |
| 2.3 | sACN E1.31 multicast receiver | Per spec #18 | Host test: parse sACN frame |
| 2.4 | Seqlock DMX buffer | Cross-core buffer per `include/seqlock.h` | Host test: 100 write-read cycles |
| 2.5 | Merge engine (HTP/LTP) | Per `merge_engine.cpp` | Host test: HTP/LTP merge |
| 2.6 | Sender tracker | 16-slot sender table per spec #05 | Host test: sender eviction |
| 2.7 | 1 ms DMX frame tick (FreeRTOS) | `dmxTxTask` on core 1, priority 19 | Hardware test: 44 Hz DMX output |
| 2.8 | RMT-based DMX transmit | Port `dmx_rmt.h` to ESP-IDF RMT v2 API | Hardware test: scope BREAK/MAB |

**Exit Criteria:** Art-Net DMX on universe 1 drives DMX output at 44 Hz with correct BREAK/MAB timing. sACN multicast received and merged. Seqlock prevents torn reads.

### Phase 3: RDM (E1.20) Controller Support

**Goal:** Full RDM controller: discovery, GET/SET PID handling, Art-Net RDM transport with cross-core response relay.

| Task | Description | Commit | Test |
|---|---|---|---|
| 3.1 | RMT TX + UART RX for RDM | Separate UART (not shared with DMX) per spec #15 | Hardware test: RDM request/response |
| 3.2 | DE/RE GPIO direction control | GPIO8/GPIO7 for outputs A/B per spec #16 | Hardware test: pin state on TX/RX |
| 3.3 | RDM engine packet framing | E1.20 transport layer per spec #09 | Host test: packet checksum |
| 3.4 | RDM discovery (DISC_UNIQUE_BRANCH) | 8s budget binary search per spec #10 | Hardware test: discover one responder |
| 3.5 | RDM task (core 1, priority 18) | Command queue + response relay per spec #11 | Hardware test: GET DEVICE_LABEL |
| 3.6 | Cross-core RDM response ring | SPSC 8x260B ring per spec #21 | Host test: ring enqueue/dequeue |
| 3.7 | Art-Net RDM dispatch + relay | Non-blocking enqueue from core 0 per spec #19 | Hardware test: ArtNet RDM GET |
| 3.8 | WebSocket RDM commands | JSON dispatch per spec #24 | Hardware test: WS discover command |

**Exit Criteria:** `idf.py build` for esp32-s3-n16r8-eth succeeds with RDM. Discovery completes under 8s for 1-64 responders. Art-Net RDM GET/SET works. Transaction IDs are monotonic.

### Phase 4: Web Configuration & Web UI

**Goal:** Full web configuration interface as a standalone web project, testable in-browser before device integration. Config schema drives web form.

| Task | Description | Commit | Test |
|---|---|---|---|
| 4.1 | Config schema + field table | Port `config_schema.cpp` to ESP-IDF NVS | Host test: schema field lookup |
| 4.2 | Config persistence (NVS) | `nvs_flash` save/load per spec #02 | Host test: NVS round-trip |
| 4.3 | Static web assets (embedded) | Gzip HTML/CSS/JS embedded per build spec #27 | Unit test: asset size under 1MB |
| 4.4 | Web frontend as standalone project | `docs/web_frontend/` as standard HTML/CSS/JS project | Browser test: form renders locally |
| 4.5 | Web routes + JSON API | `/config` GET/POST, `/info.json`, `/health` | Browser test: config round-trip |
| 4.6 | Serial console config parser | Same grammar per spec #02 Section 3 | Host test: all verbs |
| 4.7 | Board config template picker | `board-sel-modal` HTML per BUG-009 fix | Browser test: pin picker modal |

**Exit Criteria:** All config fields from `config_schema.cpp` appear in web form. `idf.py build` for esp32-s3-psram succeeds with config persistence. Frontend runs in browser without device.

### Phase 5: Ethernet (W5500 + RMII LAN8720)

**Goal:** Wired Ethernet bring-up for WT32-ETH01 (RMII) and LuxDMX-4uni (W5500 SPI), with link-loss fallback.

| Task | Description | Commit | Test |
|---|---|---|---|
| 5.1 | Ethernet init abstraction | `esp_eth` + `esp_eth_phy` per spec #31 | Hardware test: eth link up |
| 5.2 | W5500 SPI init | Per-spec pin map (CS=10/SCK=12/MOSI=11/MISO=13) | Hardware test: W5500 detected |
| 5.3 | LAN8720 RMII init | Per-spec pin map (MDIO=3/MDC=23/PWR=16/CLK=0) | Hardware test: LAN8720 link up |
| 5.4 | Link-loss fallback policy | Wired retry/AP/reboot/WiFi per spec #31 | Hardware test: cable unplugged |
| 5.5 | Net state accessor layer | `netConnected()`, `netLocalIP()` per spec #32 | Host test: interface switch |

**Exit Criteria:** `idf.py build` for wt32-eth01 succeeds with RMII Ethernet. Build for esp32-s3-n16r8-eth succeeds with W5500. Link-loss fallback works on both.

### Phase 6: OTA (Ed25519 Signed Updates)

**Goal:** Over-the-air updates with Ed25519 signature verification, three install paths (GitHub, URL, upload), background queue.

| Task | Description | Commit | Test |
|---|---|---|---|
| 6.1 | OTA key infrastructure | Port `tools/gen_ota_keys.py` + `tools/sign_ota.py` | Unit test: key generation |
| 6.2 | Ed25519 signature verify | ESP-IDF `mbedtls` per BUG-015 fix | Host test: verify signed blob |
| 6.3 | GitHub OTA path | `POST /ota/github` per BUG-003 fix | Hardware test: OTA from release |
| 6.4 | URL OTA path | `POST /ota/url` | Hardware test: OTA from URL |
| 6.5 | Upload OTA path | `POST /ota/upload` (multipart) | Hardware test: local file upload |
| 6.6 | OTA progress tracking | `otaProgPhase`/`otaProgPct` globals | Hardware test: progress JSON |
| 6.7 | Boot-update resume | Crash-guard recovery per spec #29 | Hardware test: power cycle mid-OTA |

**Exit Criteria:** All 3 OTA paths work on esp32-s3-n16r8-eth with `OTA_SIGN_ENABLED=1`. Signature verification rejects corrupt firmware. Progress reported via `/ota/status` JSON.

### Phase 7: Scene Engine + Display + Advanced Features

**Goal:** Scene presets, OLED/SPI display, syslog remote logging, alert webhooks, input router (DMX-in to network), firmware version check.

| Task | Description | Commit | Test |
|---|---|---|---|
| 7.1 | Scene engine | 1 ms fade engine per spec #08 | Host test: scene fade step |
| 7.2 | Display driver | I2C OLED / SPI per spec #37 | Hardware test: text on display |
| 7.3 | Syslog client | RFC 5424 UDP per spec #40 | Hardware test: syslog to server |
| 7.4 | Alert webhook | HTTP POST on source loss per spec #41 | Hardware test: webhook POST |
| 7.5 | Input router | DMX-in to sACN/ArtNet per spec #12 | Hardware test: DMX input frames |
| 7.6 | Firmware version check | HTTPS GET per spec #39 | Hardware test: version.json |
| 7.7 | Menu / input map | Rotary encoder + buttons per specs #43/#44 | Hardware test: menu navigation |
| 7.8 | Enc/dec | Timecode format per spec #42 | Host test: encode/decode |

**Exit Criteria:** Scenes play with 1 ms fades. Display shows status. Syslog receives logs. OTA version check works.

### Phase 8: Soak Test + CI/CD + Documentation Lock-in

**Goal:** Soak monitor, CI/CD pipeline, living documentation updated with ESP-IDF decisions.

| Task | Description | Commit | Test |
|---|---|---|---|
| 8.1 | Soak monitor | Heap watchdog per spec #38 | Hardware test: reboot on low heap |
| 8.2 | CI/CD pipeline | Build all 3 boards + run host tests | CI: all green on push |
| 8.3 | Static analysis | `idf.py analyze` + cppcheck | CI: no new warnings |
| 8.4 | On-device test automation | Wokwi simulation for ESP-IDF tests | CI: idf.py test passes |
| 8.5 | Update living docs | `codebase_index.md` + `Lessons_Learned.md` | Docs reflect ESP-IDF architecture |

**Exit Criteria:** CI pipeline green on all 3 boards. Wokwi tests pass. `codebase_index.md` and `Lessons_Learned.md` updated with ESP-IDF decisions.

---

## 3. Phase 1: Detailed Task Breakdown

### 3.1 Task Breakdown

| # | Task | Sub-tasks | Complexity |
|---|---|---|---|
| 1.1 | ESP-IDF project scaffold | Root `CMakeLists.txt`, `sdkconfig.defaults` per board, `components/` skeleton (log, board, wifi_manager, nvs_store, status_led) | Medium |
| 1.2 | Logging | `esp_log` wrapper, level filtering, serial output format, optional web sink | Low |
| 1.3 | Board detection | `Kconfig` for `TARGET_BOARD` enum, runtime `board_id` from Kconfig, pin constants from board headers | Medium |
| 1.4 | Status LED | GPIO plain PWM via `driver/ledc`, WS2812 via `driver/rmt`, 5-LED panel GPIO writes | Medium |
| 1.5 | NVS credentials | `nvs_flash_init` + `nvs_open` / `nvs_set_str` / `nvs_get_str` for WiFi SSID/PSK | Low |
| 1.6 | WiFi station | `esp_wifi_init`, `esp_wifi_set_mode(STA)`, `esp_wifi_set_config`, event handler (IP acquired/failed) | High |
| 1.7 | BOOT button | GPIO0 input with pull-up, 3s low detection in setup, debounced | Low |
| 1.8 | SoftAP + DNS | `esp_wifi_set_mode(AP)`, `esp_netif_create_default_wifi_ap`, ESP-IDF DNS server | Medium |
| 1.9 | Setup portal HTTP | `esp_http_server` with GET `/` to HTML form, POST `/setup` to save + reboot | Medium |
| 1.10 | Credential POST handler | Parse `application/x-www-form-urlencoded`, extract SSID/PSK, persist, trigger reboot | Medium |

### 3.2 Test Strategy

| Test Type | Framework | Scope | Runner |
|---|---|---|---|
| Host unit tests | CTest + check macro (ported from existing native tests) | Logging format, NVS shim round-trip, board pin lookup, config field validation | `python3 test/native/test_native.py` (modified for ESP-IDF shims) |
| Hardware-in-the-loop | Manual + Wokwi (Phase 8) | LED blink, WiFi connect, captive portal, credential persistence | `idf.py build && idf.py -p (PORT) monitor` |
| Build gate | `idf.py build` per target board | Compiles on all 3 board targets | GitHub Actions |

**New test files:**
- `test/native/logging_test.cpp` — log format, level filtering
- `test/native/board_config_test.cpp` — pin mapping per board
- `test/native/nvs_test.cpp` — save/load round-trip (ESP-IDF NVS shim)

### 3.3 Commit Sequence

Each commit is one logical feature, builds green, includes tests.

```
commit 1:  ESP-IDF project scaffold
            - Root CMakeLists.txt (idf.py project)
            - sdkconfig.defaults per board
            - components/ skeleton (log, board, wifi_manager, nvs_store, status_led)
            Gate: idf.py build succeeds for esp32-s3

commit 2:  Logging module (components/log/ + test)
            - esp_log wrapper, serial output
            - test/native/logging_test.cpp passes

commit 3:  Board detection + pin mapping (Kconfig + components/board/)
            - TARGET_BOARD Kconfig selection
            - Pin constants for ESP32-S3-N16R8, WT32-ETH01, ESP32-dev
            - test/native/board_config_test.cpp

commit 4:  Status LED driver
            - GPIO2, GPIO48, and 5-LED panel support
            - WS2812 via RMT (ESP-IDF driver)
            - Hardware test: LED blinks on all 3 boards

commit 5:  NVS credentials storage
            - nvs_flash_init + WiFi cred read/write
            - test/native/nvs_test.cpp

commit 6:  WiFi station connection
            - esp_wifi_init + connect + event handler
            - Boot animation while connecting
            - Hardware test: connects to MSI/12345678

commit 7:  BOOT button detection
            - GPIO0 3s hold detection
            - Hardware test: BOOT hold triggers portal

commit 8:  SoftAP + captive DNS
            - esp_wifi_set_mode(AP) + DNS redirect
            - Hardware test: 192.168.4.1 responds

commit 9:  Setup portal HTTP endpoint
            - esp_http_server GET / to setup form
            - Hardware test: form renders in browser

commit 10: Setup portal credential POST + reboot
            - Parse form, persist to NVS, reboot
            - Hardware test: credentials survive reboot
```

### 3.4 Board-Specific Pin/Peripheral Mapping

| Peripheral | ESP32-S3-N16R8 | WT32-ETH01 | ESP32-DevKit |
|---|---|---|---|
| **LED pin** | GPIO48 (WS2812, ledtype=2) | GPIO2 (plain, ledtype=1) | GPIO2 (plain, ledtype=1) |
| **LEDC channel** | LEDC_CH0 (GPIO48) | LEDC_CH0 (GPIO2) | LEDC_CH0 (GPIO2) |
| **WS2812 RMT** | RMT_TX_CHANNEL (GPIO48) | N/A | N/A |
| **5-LED panel** | R=GPIO1, G=GPIO2, Y=GPIO6, B=GPIO7, W=GPIO15 | N/A | N/A |
| **BOOT button** | GPIO0 | GPIO0 | GPIO0 |
| **WiFi** | Built-in | Built-in | Built-in |
| **SoftAP** | Same WiFi radio | Same WiFi radio | Same WiFi radio |
| **Status LED active state** | HIGH (WS2812 data) | HIGH (plain GPIO) | HIGH (plain GPIO) |

### 3.5 Frontend Integration Contract

#### API Endpoints (Phase 1)

| Endpoint | Method | Request Body | Response | Status Codes |
|---|---|---|---|---|
| `/` | GET | — | HTML setup form | 200 |
| `/setup` | POST | `wifi_ssid=<ssid>&wifi_psk=<psk>` | Redirect to `/` or error | 302 / 400 |
| `/info.json` | GET | — | `{board, version, wifi_connected, ip, rssi}` | 200 |
| `/log.json` | GET | — | `[{ts, level, msg}]` (last N entries) | 200 |

#### Event Schema

```
serial_log_event = {
  timestamp: uint32 (ms since boot),
  level: "INFO" | "WARN" | "ERROR",
  module: string (e.g. "wifi", "setup"),
  message: string
}
```

#### Form Field Contract

The setup page form fields (remediation for Spec #33 Open Question #2):

```html
<!-- POST to /setup -->
<input name="wifi_ssid" type="text" maxlength="32" required>
<input name="wifi_psk" type="password" maxlength="64" required>
```

---

## 4. ESP-IDF Component Structure & Kconfig Layout

### 4.1 Component Layout

```
components/
├── log/                    # Structured logging (esp_log wrapper)
|   ├── CMakeLists.txt
|   ├── Kconfig
|   ├── log.h
|   └── log.c
├── board/                  # Board detection + pin mapping
|   ├── CMakeLists.txt
|   ├── Kconfig
|   ├── board.h
|   ├── board.c
|   ├── pins_esp32s3_n16r8.h
|   ├── pins_wt32eth01.h
|   └── pins_esp32dev.h
├── wifi_manager/           # WiFi station/AP + setup portal
|   ├── CMakeLists.txt
|   ├── Kconfig
|   ├── wifi_manager.h
|   └── wifi_manager.c
├── nvs_store/              # NVS persistence abstraction
|   ├── CMakeLists.txt
|   ├── Kconfig
|   ├── nvs_store.h
|   └── nvs_store.c
├── status_led/             # LEDC/WS2812/5-LED panel
|   ├── CMakeLists.txt
|   ├── Kconfig
|   ├── status_led.h
|   └── status_led.c
├── http_server/            # ESP-IDF HTTP server + routes
|   ├── CMakeLists.txt
|   ├── Kconfig
|   ├── http_server.h
|   └── http_server.c
├── dmx_driver/             # RMT-based DMX TX (Phase 2)
|   └── ...
├── rdm_driver/             # RDM transport (Phase 3)
|   └── ...
└── proto_artnet/           # Art-Net/sACN (Phase 2)
    └── ...
```

### 4.2 Kconfig Hierarchy

**Root `Kconfig.projbuild`:**
```
main/Kconfig.projbuild
  -> chooses: TARGET_BOARD (ESP32_S3_N16R8, WT32_ETH01, ESP32_DEV)
  -> sources: components/*/Kconfig
```

**Per-component `Kconfig`:**
```
components/log/Kconfig
  -> CONFIG_LOG_DEFAULT_LEVEL
  -> CONFIG_LUXDMX_LOG_TIMESTAMP

components/board/Kconfig
  -> CONFIG_TARGET_BOARD (enum)
  -> CONFIG_LED_TYPE (off/plain/ws2812/panel)
  -> CONFIG_LED_PIN

components/wifi_manager/Kconfig
  -> CONFIG_WIFI_SSID_DEFAULT
  -> CONFIG_WIFI_PASSWORD_DEFAULT
  -> CONFIG_SETUP_PORTAL_TIMEOUT_S

components/dmx_driver/Kconfig
  -> CONFIG_DMX_TX_RATE_HZ
  -> CONFIG_MAX_OUTPUTS
```

### 4.3 Build System Integration

- Root `CMakeLists.txt` uses standard ESP-IDF `idf_build` + `app` components
- `sdkconfig.defaults.<board>` provides per-board defaults (e.g., `sdkconfig.defaults.esp32-s3`)
- Each component self-registers via `idf_component_register()` in its `CMakeLists.txt`
- PlatformIO integration: `platform = espressif32`, `framework = espidf` per target board

---

## 5. CI/CD Pipeline Sketch

### 5.1 GitHub Actions Workflow (`.github/workflows/esp-idf-ci.yml`)

```yaml
name: ESP-IDF Migration CI

on:
  push:
    branches: [main, refactor/esp-idf]
  pull_request:
    branches: [refactor/esp-idf]

env:
  BUILD_ENVS: esp32s3_psram_idf,wt32eth01_idf,esp32dev_idf

jobs:
  build:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        board: [esp32-s3, esp32, wt32-eth01]
    steps:
      - uses: actions/checkout@v4
      - name: Set up ESP-IDF
        uses: espressif/idf-actions/setup@v2
        with:
          version: "v5.2"
          target: ${{ matrix.board }}
      - name: Configure
        run: idf.py --target ${{ matrix.board }} reconfigure
      - name: Build
        run: idf.py --target ${{ matrix.board }} build
      - name: Upload artifact
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: firmware-${{ matrix.board }}
          path: _build/${{ matrix.board }}/

  host-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Set up Python
        uses: actions/setup-python@v5
        with: { python-version: "3.11" }
      - name: Generate templates
        run: python3 tools/gen_config_templates.py
      - name: Run native tests
        run: python3 test/native/test_native.py all
      - name: Upload coverage
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: coverage-report
          path: coverage/

  embedded-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install ESP-IDF
        uses: espressif/idf-actions/setup@v2
        with:
          version: "v5.2"
          target: esp32-s3
      - name: Run Unity tests
        run: idf.py --target esp32-s3 test

  static-analysis:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install ESP-IDF
        uses: espressif/idf-actions/setup@v2
        with: { version: "v5.2" }
      - name: Static analysis
        run: idf.py --target esp32-s3 analyze

  wokwi-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Run Wokwi simulation
        uses: wokwi/wokwi-ci-action@v2
        with:
          board: esp32-s3
          espIdf: v5.2
          sketch: .
```

### 5.2 Wokwi Integration

- Uses `wokwi-manifest.json` targeting `esp32-s3-devkitc`
- ESP-IDF v5.2 + `esp32-s3` target
- Tests run in simulation; physical validation deferred to hardware testing phases

---

## 6. Risk Mitigation

| Risk | Mitigation |
|---|---|
| WiFi event handler differs from Arduino | Use `esp_event_handler_instance` with explicit state machine; test connection state transitions |
| RMT v2 API differs from Arduino RMT | Write RMT wrapper with unit-testable encoder logic; verify BREAK/MAB timing on scope |
| ESP-IDF HTTP server vs AsyncWebServer | Use `esp_http_server`; AsyncWebServer not available in pure ESP-IDF |
| NVS API differences | Wrap NVS in `nvs_store` component with shim for host tests |
| Component discovery via CMake | Use `idf_component_register()` in each `CMakeLists.txt`; components auto-discovered |
| Kconfig vs. platformio.ini flags | Kconfig drives compile-time; per-board `sdkconfig.defaults.<board>` |
| Core isolation regression | Pin `dmxTxTask` to core 1 via `xTaskCreatePinnedToCore`; AsyncTCP not needed (ESP-IDF TCP/IP on core 0 natively) |

---

## 7. Living Document Maintenance

This plan itself serves as the living reference. Updates:

- **Phase completion:** Mark tasks as complete, add implementation notes
- **New BUGs:** Add ESP-IDF-specific entries (e.g., "BUG-025: esp_wifi_init requires tcpip stack init first")
- **API mapping:** Document Arduino-to-ESP-IDF API translations as they are discovered
- **Timing validation:** Record measured BREAK/MAB timing, RDM turnaround times
- **Memory budget:** Track per-phase heap usage on each target board

The `docs/codebase_index.md` and `docs/Lessons_Learned.md` files will be updated at Phase 8 to reflect the final ESP-IDF architecture.