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
| **RMT-based DMX512 output** | DMX is clocked out of the **RMT peripheral** (`/issue #64`), not the UART. This is immune to core-0 network DMA contention — the same bug that corrupted breaks under heavy WiFi. |
| **Up to 4 DMX outputs** | Expanded from 2 to **4 independent universes** (RMT channels 0–3). Outputs A & B are RDM-capable; C & D are DMX-only. |
| **PSRAM support** | An optional 8 MB octal PSRAM build (`esp32s3_psram`) moves WiFi/lwIP buffers and RDM tables to external RAM, freeing ~150 KB of internal DRAM. |
| **4-universe board support** | Dedicated `esp32s3_n16r8_eth` environment for the **LuxDMX-4uni** board (ESP32-S3-WROOM-2 N16R8 + W5500). |
| **Seqlock buffer** | A single-writer/single-reader seqlock (`include/seqlock.h`) protects the DMX frame buffer between the core-0 receive task and the core-1 transmit task — torn reads are detected and skipped, never transmitted. |
| **Crash-safe output init** | A guarded init sequence with NVS-backed crash counting progressively disables outputs if init panics, so a bad pin can never brick the device. |
| **Live config saves** | Most settings apply instantly without a reboot (universe, merge mode, TX rate, signal-loss policy, brightness). Only GPIO/driver-bound settings trigger a restart. |
| **Schema-driven config** | Every persisted setting is described once in `src/cfg/config_schema.cpp`; that table drives NVS load/save, the `/config` web form, the serial console, and the migration engine. Defaults live in `templates/*.ini`, not in `-D` macros. |

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
| **RDM (E1.20)** | DISC_UNIQUE_BRANCH discovery, GET/SET DEVICE_INFO / start address / identify / sensors on an RDM-capable output (DE/RE pin required) |
| **RDM over Art-Net** | Full Art-Net 4 RDM output gateway (ArtPoll / ArtTodRequest / ArtTodControl / ArtRdm). Discovery scheduled one transaction per DMX frame — RDM never stalls DMX output. |
| **Manual DMX control** | Set any channel from the browser via slider |
| **Blackout button** | Zero all channels instantly from browser |
| **Art-Net / Manual toggle** | Switch between protocol passthrough and manual override |
| **Remote IP config (ArtIpProg)** | A controller can read/set IP/mask/gateway or switch to DHCP over Art-Net `ArtIpProg`. Off by default (Art-Net has no auth). |

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
| **Versioned OTA** | Pick & install any past release, or auto-update to latest |
| **OTA Updates** | ArduinoOTA (IDE/CLI) + manual `.bin` upload + one-click update from luxdmx.org |

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
| `esp32s3_n16r8_eth` | ESP32-S3 | From-source | W5500 SPI | **4** | LuxDMX-4uni board: 4 universes, 8 MB PSRAM |

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
| | mode | DMX only | Reboot |

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

| V1 (monolith) | V2 (modular) |
|---|---|
| Everything in `main.cpp` | Split: `drv/` + `cfg/` + `core/` + `net/` + `app/sys/` |
| esp_dmx UART driver | Custom RMT TX (`dmx_rmt.h`) + UART RX (`uart_rx.h`) |
| 2 DMX outputs max | Up to 4 RMT-channel outputs (A/B RDM-capable, C/D DMX-only) |
| 2 board templates | Schema-driven templates (`templates/*.ini`); 33 board catalog online |
| `-D` macros for defaults | `templates/*.ini` selected by `-DDEFAULT_TEMPLATE=...` |
| NVS keys: `o0_*`/`o1_*` | Migrated to `a_*`/`b_*` (auto-migration in `nvs_migrate.cpp`) |
| NVS key: `apfb` (bool) | Migrated to `fbmode` (enum: 4 link-loss policies) |

**Upgrading from V1 firmware:** Your existing config migrates automatically on first boot. Output A maps to the new `a_*` keyspace; Output B to `b_*`. The `apFallback` boolean becomes `linkLossMode`. No data is lost.

### Breaking Changes

| Change | Impact |
|---|---|
| `esp_dmx` library removed | No compile-time dependency; DMX/RDM now first-party (`dmx_rmt.h`, `rdm_rmt.h`) |
| PlatformIO platform pinned to `pioarduino` v55.03.39 | Required for arduino-esp32 v3 / W5500 ETH support |
| ESP32-S3 builds run from-source | Brownout detector disabled (`CONFIG_ESP_BROWNOUT_DET=n`) to prevent boot-loop |
| ENC28J60 not supported | Use W5500 for wired Ethernet (same SPI bus, full hardware TCP/IP stack) |

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
