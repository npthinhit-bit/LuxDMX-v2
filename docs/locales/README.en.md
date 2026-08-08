<p align="center">
  <a href="https://luxdmx.org/video"><img src="docs/mock.png" alt="LuxDMX V2 — modular Art-Net / sACN to DMX512 gateway with live diagnostic web UI" width="100%"></a>
</p>

<p align="center">
  <img src="docs/logo.png" alt="LuxDMX V2 logo" width="120">
</p>

<h1 align="center">LuxDMX V2</h1>

<p align="center">
  <b>Open-source Art-Net / sACN (E1.31) &rarr; DMX512 gateway for ESP32 / ESP32-S3 / Ethernet.</b>
</p>

<p align="center">
  Not just another Art-Net node &mdash; a network DMX node <i>and</i> a live diagnostic tool.<br>
  Watch all 512 channels update in real time in your browser, get warned the instant
  <b>two consoles fight over a universe</b>, see per-sender FPS and frame jitter, and drive
  <b>galvanically-isolated</b> DMX out. Builds for a few dollars.
</p>

<p align="center">
  <a href="https://tombueng.github.io/LuxDMX/"><img alt="Flash in browser" src="https://img.shields.io/badge/flash%20in-browser-2dd4bf"></a>
  <a href="https://github.com/tombueng/LuxDMX/actions/workflows/build.yml"><img alt="Build" src="https://github.com/tombueng/LuxDMX/actions/workflows/build.yml/badge.svg"></a>
  <a href="https://github.com/tombueng/LuxDMX/releases"><img alt="Latest firmware" src="https://img.shields.io/github/v/tag/tombueng/LuxDMX?filter=v*&sort=semver&label=firmware"></a>
  <img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-blue">
  <img alt="Version" src="https://img.shields.io/badge/version-V2.0%20modular-blue">
  <img alt="Status: stable" src="https://img.shields.io/badge/status-stable-green">
</p>

<p align="center">
  <a href="https://tombueng.github.io/LuxDMX/"><b>⚡ Flash from your browser</b></a> &nbsp;·&nbsp;
  <a href="https://luxdmx.org/video"><b>▶ Watch the demo</b></a> &nbsp;·&nbsp;
  <a href="hardware/README.md"><b>🛠 Custom PCB</b></a> &nbsp;·&nbsp;
  <a href="#-flashing-pre-built-firmware">Install</a>
</p>

<p align="center">
  <a href="docs/locales/README.vi.md">🇻🇳 Tiếng Việt</a> &nbsp;·&nbsp;
  <a href="docs/locales/README.en.md">🇬🇧 English</a> &nbsp;·&nbsp;
  <a href="docs/locales/README.fr.md">🇫🇷 Français</a> &nbsp;·&nbsp;
  <a href="docs/locales/README.ja.md">🇯🇵 日本語</a>
</p>

| Status page | Settings page |
|---|---|
| ![Status page](docs/screenshot-status.png) | ![Settings page](docs/screenshot-config.png) |

---

## 🎯 What's New in V2 (Modular Architecture)

V2 is a ground-up **modular rewrite** of the original monolithic firmware. The codebase has been split into a clean, testable 5-layer architecture so features can evolve independently without destabilizing the core DMX path.

### Key V2 Improvements at a Glance

| V2 Feature | What changed |
|---|---|
| **Modular 5-layer architecture** | Firmware split into `drv` &rarr; `cfg` &rarr; `core` &rarr; `net` &rarr; `app/sys`, wired by a thin `main.cpp`. Each layer owns one concern and can be tested in isolation. |
| **RMT-based DMX512 output** | DMX is clocked out of the **RMT peripheral** (issue #64), not the UART. This is immune to core-0 network DMA contention &mdash; the same bug that corrupted breaks under heavy WiFi. |
| **Up to 4 DMX outputs** | Expanded from 2 to **4 independent universes** (RMT channels 0&ndash;3). Outputs A & B are RDM-capable; C & D are DMX-only. |
| **PSRAM support** | An optional 8 MB octal PSRAM build (`esp32s3_psram`) moves WiFi/lwIP buffers and RDM tables to external RAM, freeing ~150 KB of internal DRAM. |
| **4-universe board support** | Dedicated `esp32s3_n16r8_eth` environment for the **LuxDMX-4uni** board (ESP32-S3-WROOM-2 N16R8 + W5500). |
| **Seqlock buffer** | A single-writer/single-reader seqlock (`include/seqlock.h`) protects the DMX frame buffer between the core-0 receive task and the core-1 transmit task &mdash; torn reads are detected and skipped, never transmitted. |
| **Crash-safe output init** | A guarded init sequence with NVS-backed crash counting progressively disables outputs if init panics, so a bad pin can never brick the device. |
| **Live config saves** | Most settings apply instantly without a reboot (universe, merge mode, TX rate, signal-loss policy, brightness). Only GPIO/driver-bound settings trigger a restart. |
| **Schema-driven config** | Every persisted setting is described once in `src/cfg/config_schema.cpp`; that table drives NVS load/save, the `/config` web form, the serial console, and the migration engine. Defaults live in `templates/*.ini`, not in `-D` macros. |
| **Delta transmit style** | Per-output TX style: **Continuous** (free-run at the configured frame rate) or **Delta** (one DMX frame per received input packet only). Both apply live via the web UI. |
| **Configurable output mode** | Each output can be set to **DMX-only** (auto-direction RS485) or **RDM full** (DE/RE GPIO + UART RX). Setting an RTS pin automatically enables RDM mode. |
| **Transmit style source** | Tracks whether the TX style was set locally (web UI / serial) or by a controller (Art-Net), so a console can push a style and see it reflected. |
| **Extended RDM PIDs** | Full set of E1.20 typed PIDs: DEVICE_MODE, DEVICE_MODES, IDENTIFY_MODE, BURN_IN, DEVICE_HOURS, DEVICE_POWER, PERSONALITY_DESCRIPTION, SENSOR_DEFINITION/VALUE/RECORD, STATUS_MESSAGE. |
| **Sub-device enumeration** | Queries sub-device count via DEVICE_INFO; opt-in RDM device cap (default 8) limits discovery. |
| **sACN Universe Discovery + Stream Sync** | Consumes sACN Universe Discovery packets; honours per-output Stream Sync with a 500 ms commit grace &mdash; staged frames are forwarded only after the sync loss timeout. |
| **Soak-test monitor** | `LUXDMX_SOAK_TEST` build flag enables a 60-second heap watchdog that logs DRAM/PSRAM every minute and reboots if free DRAM drops below 30 KB. Exposed via `/diag/soak-stats`. |
| **Ed25519 signed OTA** | Release firmware images are Ed25519-signed; the build embeds a 32-byte public key and verifies the 64-byte signature suffix before committing an update (`OTA_SIGN_ENABLED` for production; dev builds skip verification). |
| **Background queue policy** | Configurable ArtPoll status-collection severity (disabled / advisory / warning / error) via `artnetBridgeDispatch` &mdash; controls how often the node reports background status to controllers. |

---

## ✨ Features

### Protocol & DMX

| Feature | Details |
|---|---|
| **Art-Net &rarr; DMX512** | Full 512-channel, unicast or broadcast, universe configurable (0–15) |
| **sACN / E1.31 &rarr; DMX512** | Multicast receive, universe configurable, runs alongside Art-Net |
| **Protocol selection** | Art-Net only / sACN only / Both — configurable in web UI |
| **Source merging** | Per-output HTP (highest takes precedence) / LTP (latest wins) / off, honouring the sACN priority field |
| **Jitter stat** | Real-time inter-frame timing deviation (EMA) |
| **Change log** | Live log of DMX value changes with top-N changed channels per frame |
| **Signal-loss policy** | Per-output: hold last frame (default), blackout, or stop sending. Continuous 40 Hz refresh bridges brief input gaps. |
| **Output rate & transmit style** | Per-output rate (20 / 25 / 33.3 / 40 / 41.7 fps) and style (Continuous free-run / Delta follow-input). Both apply live. |
| **Up to 4 DMX outputs** | Up to 4 independent universes; A+B are RDM-capable, C+D are DMX-only |
| **RDM (E1.20)** | DISC_UNIQUE_BRANCH discovery, GET/SET DEVICE_INFO / start address / identify / sensors / personality / status messages on an RDM-capable output (DE/RE pin required) |
| **Extended RDM PIDs** | DEVICE_MODE, IDENTIFY_MODE, BURN_IN, DEVICE_HOURS, DEVICE_POWER, PERSONALITY_DESCRIPTION, SENSOR_RECORD &mdash; full console interoperability |
| **Sub-device enumeration** | Queries sub-device count via DEVICE_INFO; opt-in RDM device cap (default 8) limits discovery per output |
| **RDM over Art-Net** | Full Art-Net 4 RDM output gateway (ArtPoll / ArtTodRequest / ArtTodControl / ArtRdm). Discovery scheduled one transaction per DMX frame &mdash; RDM never stalls DMX output. |
| **Manual DMX control** | Set any channel from the browser via slider |
| **Blackout button** | Zero all channels instantly from browser |
| **Art-Net / Manual toggle** | Switch between protocol passthrough and manual override |
| **Remote IP config (ArtIpProg)** | A controller can read/set IP/mask/gateway or switch to DHCP over Art-Net `ArtIpProg`. Off by default (Art-Net has no auth). |
| **Transmit style source** | Tracks whether the TX style was set locally (web UI / serial) or by a controller (Art-Net) &mdash; a console can push a style and see it reflected. |
| **Configurable output mode** | Each output can be set to DMX-only (auto-direction) or RDM full (DE/RE GPIO + UART RX). Setting an RTS pin enables RDM automatically. |

### Network & Connectivity

| Feature | Details |
|---|---|
| **Live Web UI** | Bootstrap 5 dark theme, WebSocket push (~10/s), all 512 channels visible |
| **Sender list** | Shows all active Art-Net / sACN senders with per-sender FPS |
| **Conflict detection** | Warning banner when multiple senders are active simultaneously |
| **Sparkline** | Per-channel history sparkline in the channel detail modal |
| **Channel labels** | Name any channel — shown in grid, modal, and change log |
| **Identify** | Flash a channel to full for ~1.5 s to physically locate the fixture |
| **Static IP or DHCP** | Configurable static IP/gateway/subnet/DNS, or automatic DHCP |
| **Mesh-aware WiFi** | Scans all channels and joins the **strongest** AP (multi-AP/mesh friendly) |
| **First-run setup portal** | On first boot (or BOOT-button held) the device opens its own `LuxDMX-setup` access point with a captive portal |
| **Selectable network mode** | Pick WiFi or wired Ethernet, and WiFi client/STA or standalone AP |
| **Standalone AP mode** | Hosts its own WiFi network at `192.168.4.1` |
| **Wired link-loss policy** | Keep retrying / open WPA2 AP / reboot / fall back to saved WiFi. Runtime watchdog applies mid-run. |
| **mDNS + DHCP hostname** | Reachable as `dmx-gateway.local` via mDNS, and the device sends its hostname over DHCP (option 12) |
| **REST API** | `GET /dmx.json`, `/senders.json`, `/log.json`, `/version.json`, `/labels.json`, `/info.json`, `/rdm.json` |
| **Versioned OTA** | Pick & install any past release, or auto-update to latest. GitHub release assets are **Ed25519-signed** &mdash; the device verifies the signature before committing. |
| **OTA Updates** | ArduinoOTA (IDE/CLI) + manual `.bin` upload + one-click update from luxdmx.org |
| **Background queue policy** | Configurable ArtPoll status-collection severity (disabled / advisory / warning / error) &mdash; controls how often the node reports background status to controllers |
| **Soak-test monitor** | `LUXDMX_SOAK_TEST` build flag enables a 60-second heap watchdog that logs DRAM/PSRAM every minute and reboots if free DRAM drops below 30 KB. Exposed via `/diag/soak-stats`. |

### Hardware & I/O

| Feature | Details |
|---|---|
| **Wired Ethernet** | W5500 (SPI, all boards), DM9051 (SPI, untested), LAN8720/IP101/RTL8201/DP83848/KSZ8081/JL1101 (RMII, WT32-ETH01) |
| **Status LED** | Plain GPIO, WS2812 RGB NeoPixel, or 5-LED panel — green = up, blue = RDM, orange = WiFi fallback, red = no network |
| **Optional displays** | I2C SSD1306 / SH1106 (128×64, 128×32) or SPI SSD1351 colour (128×128) |
| **On-unit controls** | Optional rotary encoder + up to 4 buttons drive a small on-display menu |
| **Configurable DMX pins** | Per output: universe, UART port, TX / RX / RTS GPIO — set at runtime via web UI, no recompile |
| **NVS persistence** | Universe, protocol, IP config, labels, hostname, OTA password, LED/DMX pin config survive reboots |
| **Config reset** | Hold BOOT button 3 s on startup, or via `/reset` page |
| **Remote restart** | Restart from web UI or `POST /reboot` |

---

## 📋 Prerequisites

| Requirement | Version |
|---|---|
| **PlatformIO Core** (VS Code extension recommended) | latest |
| **Python** (for `esptool`) | 3.8+ |
| **Arduino IDE** (optional, for serial OTA) | 2.0+ |
| **Supported board** | ESP32 (WROOM-32), ESP32-S3 DevKitC-1, WT32-ETH01, or LuxDMX v6 / LuxDMX-4uni PCB |

### Supported Build Environments

`platformio.ini` defines the following environments:

| Environment | MCU | Build | Network | Outputs | Notes |
|---|---|---|---|---|---|
| `esp32dev` | ESP32 (WROOM-32) | Precompiled | WiFi | 2 | Default LED on GPIO2; W5500 SPI-Ethernet opt-in |
| `esp32s3dev` | ESP32-S3 | From-source | WiFi | 2 | WS2812 LED on GPIO48; brownout detector disabled (source build) |
| `wt32eth01` | ESP32 | Precompiled | RMII Ethernet | 2 | DMN on GPIO4/5; WiFi runtime-selectable |
| `esp32s3_psram` | ESP32-S3 | From-source | WiFi | 2 | 8 MB octal PSRAM enabled; external RAM for lwIP/RDM tables |
| `esp32s3_n16r8_eth` | ESP32-S3 | From-source | W5500 SPI | **4** | LuxDMX-4uni board: 4 universes, 8 MB PSRAM, soak-test monitor |

---

## ⚙️ Installation & Usage

### ⚡ Flash from your browser — no install, no command line

Open **[the web flasher](https://tombueng.github.io/LuxDMX/)** in desktop Chrome or Edge, plug in your board, pick your model, and click flash. That's it — no Python, no esptool, no toolchain.

> The manual / scripted methods below still work for the WT32-ETH01 (which has no USB port).

### Boot mode (required for all manual methods)

1. Hold **BOOT** button
2. Press and release **EN** (or **RST**) while keeping BOOT held
3. Release BOOT — the chip is now in download mode
4. Run the flash command

> **ESP32-S3 DevKitC-1:** use the **USB-UART** port (labeled on the board), not the native USB port.

#### Windows — one-liner (PowerShell)

```powershell
Set-ExecutionPolicy -Scope Process Bypass; irm https://raw.githubusercontent.com/tombueng/LuxDMX/master/flash.ps1 | iex
```

#### macOS / Linux

```bash
pip install esptool

# ESP32 (WROOM-32)
REPO=tombueng/LuxDMX
for f in firmware.bin bootloader.bin partitions.bin boot_app0.bin; do
  curl -sL "$(curl -s https://api.github.com/repos/$REPO/releases/tags/latest \
    | python3 -c "import sys,json; assets=json.load(sys.stdin)['assets']; \
      print(next(a['browser_download_url'] for a in assets if a['name']=='$f'))")" -o $f
done

esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 460800 \
  --before default_reset --after hard_reset \
  write_flash -z --flash_mode dio --flash_freq 80m \
  0x1000 bootloader.bin 0x8000 partitions.bin \
  0xe000 boot_app0.bin 0x10000 firmware.bin
```

> Replace `/dev/ttyUSB0` with your port (`/dev/tty.usbserial-*` on macOS).

### Build from source (PlatformIO)

```bash
# Install PlatformIO
pip install platformio

# Build for your board
pio run -e esp32dev      # ESP32 WROOM-32
pio run -e esp32s3dev    # ESP32-S3 DevKitC-1
pio run -e wt32eth01     # WT32-ETH01
pio run -e esp32s3_psram # ESP32-S3 with PSRAM
pio run -e esp32s3_n16r8_eth  # LuxDMX-4uni (4 universes)

# Flash (first flash via USB)
pio run -e esp32s3dev --target upload
```

> **Upload fails ("Wrong boot mode")?** Hold BOOT, tap EN/RST, release BOOT — the chip enters download mode. Retry upload.

### Project Structure

```
LuxDMX-v2/
├── src/
│   ├── main.cpp              ← thin wiring entry point (setup + loop)
│   ├── cfg/                  ← config engine: schema, core, serial, NVS migration
│   │   ├── config_schema.cpp ← THE field table (one row per setting)
│   │   ├── config_core.cpp   ← NVS load/save, template resolution
│   │   ├── config_serial.cpp ← serial console (key=value interface)
│   │   └── nvs_migrate.cpp   ← V1→V2 NVS key migration
│   ├── drv/                  ← low-level hardware drivers
│   │   ├── dmx_rmt.h         ← RMT-based DMX512 transmit engine
│   │   ├── uart_rx.h         ← RX-only UART for RDM responses
│   │   └── gpio_dir.h        ← DE/RE GPIO control for RDM
│   ├── core/                 ← DMX/RDM protocol core
│   │   ├── output_init.cpp   ← RMT channel + UART init, crash-safe guard
│   │   ├── merge_engine.cpp  ← HTP/LTP per-output source merging
│   │   ├── sender_tracker.cpp← active sender tracking + conflict detection
│   │   ├── rdm_engine.cpp    ← RDM E1.20 controller (RMT-TX + UART-RX)
│   │   ├── rdm_disc.cpp      ← DISC_UNIQUE_BRANCH binary search discovery
│   │   └── stats.cpp         ← shared runtime statistics
│   ├── net/                  ← network protocols
│   │   ├── artnet.cpp        ← Art-Net receive + RDM bridge
│   │   ├── sacn.cpp          ← sACN/E1.31 multicast receive
│   │   ├── ethernet.cpp      ← W5500/DM9051 SPI + LAN8720 RMII
│   │   ├── network.cpp       ├── WiFi STA/AP, mesh-aware strongest-AP join
│   │   ├── websocket.cpp     ← WebSocket live push (binary + JSON frames)
│   │   ├── ws_frame.cpp      ← binary frame builder
│   │   └── web_server.cpp    ← AsyncWebServer route registration
│   ├── sys/                  ← system/platform tasks
│   │   ├── tasks.cpp         ← FreeRTOS task creation (core-pinned)
│   │   ├── led_status.cpp    ← status LED driver
│   │   ├── display.cpp       ← OLED/SPI display driver
│   │   ├── ota.cpp           ← OTA boot update + init
│   │   └── sys_platform.cpp  ← board identity, reboot scheduling
│   ├── app/                  ← application layer
│   │   ├── input_map.h       ← encoder/button → nav event mapping
│   │   ├── menu.h            ← on-display menu system
│   │   └── enc_decode.h      ← quadrature decoder
│   ├── pages/                ← web UI HTML files (plain HTML)
│   ├── assets/               ← images/CSS served by the ESP32
│   └── generated/            ← auto-created at build time (gitignored)
├── include/                  ← public headers (config_schema.h, output.h, seqlock.h, ...)
├── templates/                ← per-board default values (_base.ini, luxdmx_4uni.ini)
├── lib/EmbeddedConfig/       ← reusable schema-driven config engine (NVS + serial)
├── tools/                    ← build/dev tooling (gen_config_templates.py)
├── test/native/              ← host-side config round-trip test
└── platformio.ini
```

> **Build pipeline:** Before every `pio run`, `extra_scripts.py` converts `src/pages/*.html` and `src/assets/*` into C `PROGMEM` arrays in `src/generated/*.h`, and embeds `templates/*.ini` into `src/generated/config_templates.gen.h`. Dynamic values use `{{PLACEHOLDER}}` tokens substituted at request time.

---

## 🖥️ Web UI

The HTTP server and WebSocket are served by ESPAsyncWebServer (non-blocking), so the web UI never stalls DMX output. Pages are gzip-compressed.

### Pages

| URL | Method | Function |
|---|---|---|
| `/` | GET | Live status + 512-channel DMX grid |
| `/config` | GET / POST | Change universe, protocol, per-output merge mode, signal-loss policy, static IP, hostname, OTA password, LED config, DMX pins, on-unit controls |
| `/reset` | GET / POST | Clear WiFi credentials, reboot to AP mode |
| `/reboot` | POST | Restart device (**POST only**) |
| `/rdm` | GET | RDM controller page |
| `/setup` | GET / POST | First-run setup portal |

### REST API

| Endpoint | Method | Function |
|---|---|---|
| `/dmx.json` | GET | All 512 values, fps, rssi, uptime, heap, manual mode flag |
| `/senders.json` | GET | Active Art-Net / sACN senders |
| `/log.json` | GET | Recent DMX change log entries |
| `/version.json` | GET | Current firmware version + update-available flag |
| `/info.json` | GET | Current settings + status (SSID, IP, universe, board, etc.) |
| `/labels.json` | GET | Channel labels |
| `/labels` | POST | Store the full labels object |
| `/rdm.json` | GET | RDM controller state + discovered fixtures (TOD) |
| `/rdm/discover` | GET | Trigger RDM discovery sweep |
| `/rdm/setaddr` | GET | Set fixture start address (`?uid=...&addr=1..512`) |
| `/rdm/identify` | GET | Toggle fixture identify (`?uid=...&on=0/1`) |
| `/rdm/merge` | GET | Set output merge mode (`?out=0&mode=0/1/2`) |
| `/led/bright` | GET | 5-LED panel per-colour brightness (`?r=&g=&b=&y=&w=`, `&save=1` to persist, `&test=1` for calibration) |
| `/ota/upload` | POST | Upload and flash a local `firmware.bin` |
| `/ota/github` | POST | Install a release (`version=latest` or `1.0.N`) |
| `/ota/url` | POST | Install a `.bin` from any URL (`url=http://host/firmware.bin`) |
| `/ota/status` | GET | Live progress of in-flight install (`{phase,pct}`) |
| `/autoupdate` | POST | Toggle auto-update (`enabled=0/1`) |
| `/health` | GET | Health check with per-output status, network info, alerts |
| `/diag/soak-stats` | GET | Soak-test monitor stats (DRAM/PSRAM free, uptime) &mdash; only with `LUXDMX_SOAK_TEST` build |
| `/config/export` | GET | Export full config as JSON (`?include_credentials=1` to include passwords) |
| `/config/import` | POST | Import config from JSON (`config=<json>`) |

### WebSocket (`ws://<device>/ws`)

Binary status/DMX frame pushed ~10&times;/s:

```
Bytes  0–1    fps × 10           uint16 big-endian (aggregate)
Bytes  2–3    link metric        int16  (≤0 = WiFi RSSI dBm, ≥10 = wired Mbps, 1 = AP)
Bytes  4–7    free heap          uint32 big-endian
Bytes  8–11   uptime (s)         uint32 big-endian
Byte   12     active sender count uint8
Byte   13     source status       uint8 (0 = normal, 1 = conflict, 2 = merging)
Bytes  14–15  jitter × 10 (ms)   uint16 big-endian
Bytes  16–527 DMX ch 1–512       uint8[512]
Bytes  528…   per-output fps × 10 uint16 × number of outputs
```

Browser &rarr; ESP32 (JSON text commands):

```json
{ "type": "set",      "ch": 1,  "val": 200 }
{ "type": "mode",     "manual": true       }
{ "type": "blackout"                       }
{ "type": "identify", "ch": 5              }
{ "type": "viewout",  "out": 1             }
{ "type": "rdm",      "action": "discover" }
{ "type": "rdm",      "action": "setaddr",  "uid": "4c5812345678", "addr": 1 }
{ "type": "rdm",      "action": "identify", "uid": "4c5812345678", "on": true }
{ "type": "rdm",      "action": "setpers",  "uid": "4c5812345678", "pers": 2 }
{ "type": "rdm",      "action": "setlabel", "uid": "4c5812345678", "label": "My Fixture" }
```

---

## 🔧 Configuration (V2 Schema-Driven)

V2 uses a **schema-driven configuration** system. Every persisted setting is described once as a row in `src/cfg/config_schema.cpp`, and that table drives:

- NVS load/save (`cfgcore::load()` / `save()`)
- The `/config` web form (auto-generated from the schema)
- The serial console (`cfgserial::poll()`)
- The native host test (round-trip validation)

**Resolution order:** neutral value (from constraint) &rarr; active board template (`templates/*.ini`) &rarr; saved NVS value.

### Board Templates

Defaults live in `templates/*.ini`, selected at compile time by `-DDEFAULT_TEMPLATE=...`. Each board's template extends `_base`:

| Template | Board | Notes |
|---|---|---|
| `_base` | All | Global defaults: hostname, LED, network, W5500/RMII pins |
| `esp32dev` | ESP32 DevKit | GPIO2 LED, GPIO17/16 DMX, W5500 on VSPI |
| `esp32s3dev` | ESP32-S3 DevKitC-1 | GPIO48 WS2812, GPIO17/16 DMX |
| `wt32eth01` | WT32-ETH01 | RMII LAN8720, GPIO4/5 DMX |
| `luxdmx_v6` | LuxDMX v6 PCB | 5-LED panel, W5500 SPI3, RTS/DE=8 |
| `luxdmx_4uni` | LuxDMX-4uni | 4 universes, 8 MB PSRAM, 5-LED panel |

A v6/4uni owner flashes the generic `esp32s3dev` / `esp32s3_n16r8_eth` build and selects the matching board template in `/config &rarr; Hardware board` for the full pin map.

### Persistent Settings (NVS)

| Category | Key | Default | Live-reboot? |
|---|---|---|---|
| **Identity** | hostname | `dmx-gateway` | Live |
| | boardSel | (detected) | Reboot |
| | otaPassword | `dmxota` | Reboot |
| | protocol | `Both (Art-Net + sACN)` | Reboot |
| **Status LED** | ledPin | board default | Reboot |
| | ledType | board default | Reboot |
| | ledBrR/G/Y/B/W | 255 / 255 / 255 / 255 / 255 | Live |
| **Network** | useEthernet | false | Reboot |
| | wifiMode | STA (client) | Reboot |
| | wifiSsid / wifiPsk | (none) | Reboot |
| | staticIp | false | Reboot |
| | ip / gateway / subnet / dns | (DHCP) | Reboot |
| | linkLossMode | keep retrying | Reboot |
| | ipProg | off | Reboot |
| | apPassword | (none) | Reboot |
| **RDM** | artnetRdm | true | Reboot |
| | rdmMaxDev | 0 (auto) | Reboot |
| **Updates** | autoUpdate | false | Reboot |
| **DMX Output A** | enabled | on | Reboot |
| | universe | 0 | Live |
| | port | 1 | Reboot |
| | txPin | 17 | Reboot |
| | rxPin | 16 | Reboot |
| | rtsPin | -1 | Reboot |
| | mergeMode | off | Live |
| | lossMode | hold | Live |
| | txRate | 40 fps | Live |
| | txStyle | Continuous | Live |
| | txStyleSrc | Local | Live |
| | mode | DMX only | Reboot |
| | net | 0 | Reboot |
| | subnet | 0 | Reboot |
| | sacnUniverse | 0 (auto) | Reboot |

---

## 🔌 DMX Outputs

LuxDMX V2 drives up to **4 independent DMX outputs** — each its own universe, RMT channel, and RS485 transceiver — configurable at runtime under **Settings &rarr; DMX Outputs** (no recompile).

| Output | RMT Ch | UART (RDM RX) | DMX TX GPIO | DMX RX GPIO | RDM DE/RE | Modes |
|---|---|---|---|---|---|---|
| A | 0 | UART1 | 17 | 16 | 8 (v6/4uni) | DMX / RDM-full |
| B | 1 | UART2 | 16 | 15 | 7 (4uni) | DMX / RDM-full |
| C | 2 | — | 5 | — | — | DMX-only |
| D | 3 (DMA) | — | 6 | — | — | DMX-only |

### Per-output settings

| Setting | Default (A) | Default (B) | Default (C/D) | Description |
|---|---|---|---|---|
| Enabled | on | off | off | Whether this output drives a DMX line |
| Universe | 0 | 1 | 2 / 3 | Art-Net universe (sACN = universe + 1) |
| UART port | 1 | 2 | 0 | UART number for RDM RX |
| TX pin | 17 | 16 | 5 / 6 | Data-out GPIO |
| RX pin | 16 | 15 | -1 | Data-in GPIO (needed for RDM) |
| RTS / DE pin | -1 | 7 | -1 | Direction-control (required for RDM) |
| Merge mode | off | off | off | off / HTP / LTP |
| Signal-loss | hold | hold | hold | hold / blackout / stop |
| TX rate | 40 fps | 40 fps | 40 fps | 20 / 25 / 33.3 / 40 / 41.7 fps |
| TX style | Continuous | Continuous | Continuous | Continuous (free-run) / Delta (follow input) |
| Output mode | DMX only | DMX only | DMX only | DMX only / RDM full (DE/RE). Setting an RTS pin auto-enables RDM. |
| Style source | Local | Local | Local | Local (web/serial) / Art-Net (controller push) |

### Safe GPIO Guide

**ESP32-S3 — free GPIOs for additional outputs:** 5, 6, 7, 8, 15, 18, 21

**Avoid:** 26–37 (SPI flash / PSRAM — will crash), 19/20 (USB), 43/44 (serial), 0/45/46 (strapping), 48 (WS2812 LED).

**WT32-ETH01:** GPIO16 is LAN8720 PHY power — avoid. DMX on GPIO4/5.

### Upgrade & Crash Safety

- **Single-universe devices are unaffected.** Migration maps the old single-output config to Output A and leaves B/C/D disabled.
- **No configuration can brick the device.** A crash counter in NVS progressively disables outputs if init panics — keep A &rarr; all off until the web UI is reachable.

---

## 🌐 Network Mode

### WiFi (Client / STA)

On first boot (or after a WiFi reset, or with BOOT held), LuxDMX opens its own setup AP and captive portal:

- **SSID:** `LuxDMX-setup` (open, first-run physical access)
- Pick **Join my WiFi** or **Use as access point**
- Device reboots into the selected mode

**Mesh / multi-AP:** LuxDMX scans all channels and joins the **strongest** AP for your SSID.

### Standalone AP Mode

```
WiFi mode = AP
reachable at 192.168.4.1
```

### Wired Ethernet

- **W5500 (SPI):** CS / SCK / MOSI / MISO / INT / RST configurable in `/config`. Default pins match classic ESP32 VSPI.
- **LAN8720 (RMII):** WT32-ETH01 (default) or any ESP32 + RMII PHY. PHY family, address, MDC/MDIO/RST/GPIO0 CLK all configurable.
- **Link-loss policy:** keep retrying / open WPA2 AP / reboot / join WiFi. Applied by a runtime watchdog even if the cable is pulled mid-run.

### Remote IP Config Over Art-Net (ArtIpProg)

A node that ends up on an unreachable address can be renumbered over Art-Net `ArtIpProg` (opcode `0xf800`) from a controller (e.g. DMX-Workshop). **Off by default** — Art-Net has no auth, so anyone on the wire can change the address while it's on.

---

## 🎛 On-Unit Controls

Optional rotary encoder + up to 4 buttons drive a small on-display menu to set universe, protocol, etc. without a phone or PC. All wiring choices are synthesised into a usable nav alphabet:

| Physical input | Short press | Long press |
|---|---|---|
| Encoder turn | INC / DEC | — |
| Encoder push | ENTER | BACK |
| Next button | INC | ENTER |
| Prev button | DEC | ENTER |
| Enter button | ENTER | BACK |
| Single button only | INC | ENTER |

Settings: encoder A/B/push pins, step count/direction, button pins + active-high/low, button roles (off / enter / back / next / prev). The menu always carries an **Exit** item, so even a lone button can navigate out.

---

## 🖼 Status LED

| State | WS2812 RGB | 5-LED panel | Plain GPIO |
|---|---|---|---|
| Booting | white blink | Knight-Rider sweep | blink |
| Up, idle | green (solid) | green (solid) | on |
| DMX incoming | green, slow 2s blink | green, slow 2s blink | slow blink |
| RDM activity | green + blue = cyan | green + blue lit | on |
| WiFi fallback | orange (solid) | amber (solid) | on |
| No network | red (solid) | red | off |
| Setup portal | purple | purple | on |

**Per-colour brightness (5-LED panel):** PWM-dim each colour independently (`ledbrr`/`ledbrg`/`ledbry`/`ledbrb`/`ledbrw`, 0–255). Tune live via `/led/bright?test=1`.

---

## 📱 Serial Configuration (Recovery)

If a board can't get on the network, plug it in over USB, open a serial monitor at 115200 baud, and type `help`:

| Command | What it does |
|---|---|
| `dump` | Print every setting as `key=value` (passwords masked) |
| `key=value [key=value ...]` | Set one or more fields |
| `get <key>` / `set <key> <value>` | Read / write a single field |
| `save [reboot]` | Persist to NVS, optionally reboot |
| `wifi <ssid> [pass]` | Set WiFi credentials and reconnect |
| `reboot` / `factory` | Restart / wipe config and restart |

---

## 🧪 Testing

```bash
cd docs && npm install && npx playwright install chromium
LUXDMX_HOST=dmx-gateway.local npm test
```

End-to-end Playwright tests drive a **live device** — real Art-Net/sACN packets over the network, asserting the REST API, WebSocket, and web UI. The native config round-trip test (`test/native/`) compiles the config engine on the host against small shims — no framework required.

---

## 📦 Hardware

> ### 🛠 Custom PCB — LuxDMX v6
>
> Open-source 4-layer PCB: ESP32-S3 with WiFi + wired Ethernet (W5500), two galvanically-isolated DMX universes, 802.3af PoE or USB-C, 5-LED status panel, BOOT/RST buttons, optional OLED/TFT. DMX512-A Protected (ANSI E1.11 Annex C).
>
> **→ Full design, BOM, gerbers & JLCPCB guide: [`hardware/`](hardware/README.md)**

The simpler **breadboard / module** build (ESP32 DevKit + isolated RS485 module) is perfect for getting started. See the original README for full wiring diagrams and BOM tables.

---

## 🔄 Migration Guide: V1 &rarr; V2

V2 is a ground-up **modular rewrite**. This section maps every V1 concept to its V2 equivalent so you can reason about the two side by side, and explains exactly what changes (and what stays the same) when you flash a V2 build onto a device running V1 firmware.

### Architecture: Monolith &rarr; 5-layer modular

The original V1 firmware is a single ~5,000-line `main.cpp` that mixes pin definitions, config persistence, Art-Net/sACN parsing, DMX I/O, the web server, RDM, and the serial console — all in one translation unit. V2 splits this into a disciplined 5-layer architecture, each layer owning one concern and testable in isolation.

| Layer | V1 (`main.cpp`) | V2 (modular) | Key files |
|---|---|---|---|
| **drv** (drivers) | UART TX via `esp_dmx` library | RMT hardware TX + RX-only UART + DE/RE GPIO | `src/drv/dmx_rmt.h`, `src/drv/uart_rx.h`, `src/drv/gpio_dir.h` |
| **cfg** (config) | `#define DEF_*` macros for defaults; NVS read/write inline | Schema-driven table drives NVS load/save, serial console, web form | `src/cfg/config_schema.cpp`, `src/cfg/config_core.cpp`, `src/cfg/nvs_migrate.cpp` |
| **core** (DMX/RDM protocol) | DMX buffer + merge inline in `main.cpp` | Seqlock-protected frame buffer, merge engine, sender tracking, frame router, RDM engine | `src/core/dmx_buffer.cpp`, `src/core/merge_engine.cpp`, `src/core/sender_tracker.cpp`, `src/core/rdm_engine.cpp`, `src/core/rdm_disc.cpp` |
| **net** (network) | `ArtnetWifi` library + inline WiFi/Ethernet | Self-implemented Art-Net/sACN + native W5500/RMII + AsyncWebServer + WebSocket + OTA | `src/net/artnet.cpp`, `src/net/sacn.cpp`, `src/net/websocket.cpp`, `src/net/ota.cpp` |
| **sys/app** (system) | Inline FreeRTOS tasks + inline LED/display | Pinned core-0/core-1 tasks, crash-guard, soak monitor, OTA rollback | `src/sys/tasks.cpp`, `src/sys/led_status.cpp`, `src/sys/soak_monitor.cpp` |

**`main.cpp` (V2)** is a thin ~130-line wiring file: it calls `nvs_migrate::migrateNvsKeys()`, `cfgcore::load()`, `outputInitAll()`, `startSacn()`, `webRegisterRoutes()`, then `createTasks()`. All real logic lives in the layers.

### DMX Transmission: UART &rarr; RMT

| Aspect | V1 | V2 |
|---|---|---|
| **Peripheral** | UART + GPTimer ISR (via `esp_dmx` library) | **RMT peripheral** — hardware-clocked symbol stream, no CPU timing loop |
| **Bug fixed** | Core-0 WiFi/lwIP DMA contention delayed the break/MAB timer ISR, producing malformed frames under heavy traffic (issue #64) | RMT clocks entirely in hardware; if the refill ISR is late the line just idles (a benign extra mark), never corrupts a break — see `src/drv/dmx_rmt.h:2-9` |
| **Library dependency** | `someweisguy/esp_dmx` | None — first-party `dmx_rmt.h` |
| **RDM transport** | Same UART, switched half-duplex (DE/RE GPIO toggled, direction reconfigured at runtime) | RMT-TX for requests + **separate RX-only UART** for responses (`src/drv/uart_rx.h`); never released mid-frame, DMX runs uninterrupted between RDM ops |

### Outputs: 2 &rarr; 4 universes

| Aspect | V1 | V2 |
|---|---|---|
| **Max outputs** | 2 (ESP32 has 3 UARTs; UART0 is the serial console) | 4 (RMT channels 0–3; ESP32-S3 `#error` at >4 — see `include/config_schema.h:69`) |
| **RMT channels** | N/A (UART-based) | Ch 0–3; only ch 3 has DMA on the S3 — others use ISR refill (see `src/drv/dmx_rmt.h:101`) |
| **Output A** | UART1, GPIO17/16, RDM if DE/RE pin set | RMT ch 0, UART1 RX for RDM, DE/RE GPIO configurable |
| **Output B** | UART2, GPIO16/15, RDM if DE/RE pin set | RMT ch 1, UART2 RX for RDM, DE/RE GPIO configurable |
| **Outputs C/D** | N/A | RMT ch 2 / ch 3 (DMA-capable on S3), DMX-only (no UART RX) |
| **Per-output mode** | Implicit (auto-direction RS485 or RDM based on pin presence) | Explicit `output_mode_t` enum: DMX-only vs RDM-full (`include/output.h:11`); setting an RTS pin auto-enables RDM (`resolveOutputMode()` at `include/output.h:28`) |

### Configuration: Macros &rarr; Schema-driven templates

| Aspect | V1 | V2 |
|---|---|---|
| **Defaults source** | `-DDEF_*` macros in `platformio.ini` build flags | `templates/*.ini` files selected by `-DDEFAULT_TEMPLATE=...` (embedded into firmware by `extra_scripts.py`) |
| **Resolution order** | Macro default, overridden by NVS | Neutral (from constraint) → active board template → saved NVS value |
| **Field table** | Inline `Preferences` get/put scattered through `main.cpp` | Single table in `src/cfg/config_schema.cpp` (`CONFIG_FIELDS[]` + `OUTPUT_FIELDS[]`) drives NVS, serial console, web form, and native test |
| **Per-output keys** | `o0_tx`, `o0_uni`, `o1_tx`, `o1_uni` (2 outputs) | `a_tx`, `a_uni`, `b_tx`, ... `d_tx` (4 outputs, letter prefixes) |
| **NVS migration** | No migration — direct key access | `src/cfg/nvs_migrate.cpp:13` — one-shot pass: `o0_*`→`a_*`, `o1_*`→`b_*`, `apfb`→`fbmode` |
| **Board templates** | Hardcoded `#ifdef` per environment | `templates/_base.ini` extended by per-board templates — 33 boards in the online catalog |
| **Link-loss policy** | `apFallback` (bool: true = open WiFi AP) | `linkLossMode` (enum: 0=keep retrying, 1=open WPA2 AP, 2=reboot, 3=join WiFi) — never opens an unsecured AP |

### Network Stack

| Aspect | V1 | V2 |
|---|---|---|
| **Art-Net** | `rstephan/ArtnetWifi` library | Self-implemented; full opcode dispatch (ArtPoll, ArtPollReply, ArtAddress, ArtIpProg, ArtSync, ArtNzs, ArtTod\*, ArtRdm) in `src/net/artnet.cpp` + `src/net/artnet_bridge.cpp` |
| **sACN** | `ArtnetWifi` library's sACN path | Self-implemented in `src/net/sacn.cpp` |
| **Library deps** | ArtnetWifi, Adafruit NeoPixel, Adafruit GFX, Adafruit SSD1306, Adafruit SH110X, Adafruit SSD1351 | **Only** `ESP32Async/AsyncTCP` + `ESP32Async/ESPAsyncWebServer`; no Adafruit, no ArtnetWifi |
| **AsyncTCP** | Default platform config (core 0 or 1, small queue) | Pinned to core 0 with 16 KB stack + 128 queue (`platformio.ini:43-47`) so it never preempts RDM on core 1 |
| **W5500 Ethernet** | `ETH.h` W5500 support via `ETH_PHY_W5500` | Same — but the SPI module pins are fully runtime-configurable (previously build-time) |
| **RMII Ethernet** | WT32-ETH01 only | W5500 SPI + LAN8720 RMII + IP101/RTL8201/DP83848/KSZ8081/JL1101 — selectable at runtime |

### Task Scheduling & Core Affinity

| Aspect | V1 | V2 |
|---|---|---|
| **DMX task** | `loop()`-driven, runs on core 0 (shared with WiFi/lwIP) | Dedicated `dmxTxTask` — **core 1, priority 19**, 1 ms tick via `vTaskDelayUntil` (`src/sys/tasks.cpp:82`) |
| **Network task** | `ArtnetWifi` + AsyncTCP callbacks on core 0 | Dedicated `netRxTask` — **core 0, priority 5**, bounded to 64 packets/call (`tasks.cpp:144`) |
| **RDM service** | Called from `loop()` | Serviced inside `dmxTxTask` on every 1 ms tick (not just per-DMX-frame) (`tasks.cpp:139`) — keeps discovery fast even on static looks |
| **LED/display** | Inline in `loop()` | Dedicated `ledTask` / `displayTask` at low priority |

### RDM

| Aspect | V1 | V2 |
|---|---|---|
| **Controller PID set** | DISC_UNIQUE_BRANCH, DEVICE_INFO, DMX_START_ADDRESS, IDENTIFY_DEVICE | Plus: DEVICE_MODE, DEVICE_MODES, IDENTIFY_MODE, BURN_IN, DEVICE_HOURS, DEVICE_POWER, PERSONALITY_DESCRIPTION, SENSOR_DEFINITION/VALUE/RECORD, STATUS_MESSAGE (`src/core/rdm_typed.cpp:138-242`) |
| **Sub-device enumeration** | Not implemented | `rdmSubDeviceCount()` queries DEVICE_INFO; opt-in cap via `rdmMaxDev` (default auto) |
| **Transport** | UART half-duplex (DE/RE toggled) | RMT-TX + RX-only UART (no direction switching, DMX never interrupted) |
| **Discovery scheduling** | One transaction per DMX frame | One transaction per DMX frame; discovery is a binary search (`DISC_UNIQUE_BRANCH`) with 8-second budget cap (`src/core/rdm_disc.cpp:66`) |

### Transmit Style & Output Rate

| Aspect | V1 | V2 |
|---|---|---|
| **Output rate** | Fixed 40 fps free-run on all outputs | Per-output configurable: 20 / 25 / 33.3 / 40 / 41.7 fps (`txRate` enum) |
| **Frame style** | Free-run only (clock at the configured rate regardless of input) | **Continuous** (free-run) or **Delta** (one frame per received input packet, clamped to 22.76 ms wire minimum, auto-fallback to free-run after 800 ms idle) |
| **Style source tracking** | N/A | `txStyleSrc`: tracks whether the style was set locally (web/serial) or by a controller (Art-Net `ArtAddress`) |
| **Source merging** | HTP / LTP per output, `SOURCE_TIMEOUT_MS = 4000` | Same, but now in a dedicated `src/core/merge_engine.cpp` |

### Web UI & Config Lifecycle

| Aspect | V1 | V2 |
|---|---|---|
| **Web pages** | Inline `PROGMEM` HTML strings in `main.cpp` | `src/pages/*.html` converted to `PROGMEM` by `extra_scripts.py` → `src/generated/*.h` |
| **Dynamic values** | `String::replace()` on `{{PLACEHOLDER}}` tokens | Same mechanism, but the `/config` form is **auto-generated from the schema table** |
| **Config apply** | Reboot required for all changes | **Live** for: universe, merge mode, loss mode, TX rate, TX style, LED brightness, protocol, hostname; **Reboot** for: GPIO pins, LED/display type, UART port, network settings (driven by `CFG_LIVE` vs `CFG_REBOOT` flags in `config_schema.cpp`) |
| **Config import/export** | `dump` serial command + NVS | `/config/export` (JSON, with `&include_credentials=1`), `/config/import` (POST JSON), serial `dump`/`save` |
| **OTA** | ArduinoOTA + `httpUpdate` from luxdmx.org | ArduinoOTA + web upload + GitHub release + URL install + **Ed25519 signed** firmware (`src/net/ota_sign.cpp`, `OTA_SIGN_ENABLED` for production) |
| **Soak test** | Not available | `LUXDMX_SOAK_TEST` build flag — 60-second heap watchdog on `esp32s3_n16r8_eth`, logs DRAM/PSRAM every minute, reboots if free DRAM < 30 KB (`/diag/soak-stats`) |

### Crash Safety

| Aspect | V1 | V2 |
|---|---|---|
| **Output init** | Inline in `setup()`, panics brick the device | Guarded init (`dmxInitGuardBegin()`/`dmxInitGuardEnd()`) with NVS-backed crash counting — progressively disables outputs if init panics (`src/sys/tasks.cpp:42`) |
| **OTA rollback** | `otatries` counter, 3 boot attempts max | Same mechanism, but moved to `src/net/ota.cpp` with explicit `OTA_BOOT_TRIES = 3` and 60-second stable-uptime zeroing |

### Breaking Changes (upgrade checklist)

| Change | Impact |
|---|---|
| `esp_dmx` library removed | No compile-time dependency — DMX/RDM are now first-party (`src/drv/dmx_rmt.h`, `src/core/rdm_engine.h`); the old `someweisguy/esp_dmx` types were re-declared in `include/rdm_types.h` as a drop-in |
| PlatformIO platform pinned to `pioarduino` v55.03.39 | Required for arduino-esp32 v3 / W5500 ETH support; the mainline `espressif32` platform is stuck at v2.x |
| ESP32-S3 builds run from-source | `CONFIG_ESP_BROWNOUT_DET=n` via `custom_sdkconfig` — the IDF brownout detector fires before `setup()`, causing a boot-loop on real S3 hardware |
| ENC28J60 not supported | Use W5500 for wired Ethernet (same SPI bus, full hardware TCP/IP stack) |
| `ArtnetWifi` library removed | Art-Net/sACN protocol parsing is now self-implemented in `src/net/artnet.cpp` + `src/net/sacn.cpp` |
| Adafruit libraries removed | Display/LED drivers are stubs (`src/sys/led_status.cpp`, `src/sys/display.cpp`); WS2812 and OLED support will return as optional modules |
| `MAX_OUTPUTS` raised to 4 | Output C and D are DMX-only (no RDM); the old 2-output config migrates to A/B automatically |
| `apFallback` (bool) &rarr; `linkLossMode` (enum) | 0 = keep retrying (was `false`); 1 = open WPA2 AP (was `true`). Values 2 (reboot) and 3 (join WiFi) are new. The AP **requires a password** — `linkLossMode=1` without `apPassword` falls back to retry |

### Migration Path

**Upgrading from V1 firmware:** Your existing config migrates automatically on first boot. `nvs_migrate::migrateNvsKeys()` (called at `src/main.cpp:41`) performs a one-shot pass:

- Output A keys: `o0_*` &rarr; `a_*` (e.g. `o0_tx` &rarr; `a_tx`) — `nvs_migrate.cpp:10-11`
- Output B keys: `o1_*` &rarr; `b_*` (e.g. `o1_uni` &rarr; `b_uni`)
- `apfb` (bool) &rarr; `fbmode` (enum: `0` or `2` if `apfb=false`, `1` if `apfb=true`) — `nvs_migrate.cpp:42-47`

The `cfgcore::load()` function also has an inline fallback for output-0 legacy keys (`config_core.cpp:179`), so even a partial NVS that skipped `migrateNvsKeys()` still resolves correctly. No data is lost; outputs C and D start disabled and can be enabled in `/config`.

If a crash occurs during the first V2 boot, the crash-guard counter in NVS (`dmxgw` namespace, key `dmxcrash`) progressively disables outputs starting from the highest index. Clear it by power-cycling the device to a stable boot (60 s of uptime resets the counter via `dmxInitGuardEnd()`), or via the `/reset` page for a full factory wipe.

---

## 📄 License

MIT — do whatever you want, attribution appreciated.

---

<p align="center">
  <sub>
    <a href="https://tombueng.github.io/LuxDMX/">⚡ Flash from browser</a> &nbsp;·&nbsp;
    <a href="https://luxdmx.org/video">▶ Watch the demo</a> &nbsp;·&nbsp;
    <a href="hardware/README.md">🛠 Custom PCB</a>
  </sub>
</p>
