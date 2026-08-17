# Technical Reference Index — LuxDMX-v2

## 1. System Overview

LuxDMX-v2 is an Art-Net (E1.31) / sACN gateway firmware for ESP32 / ESP32-S3 / WT32-ETH01 / LuxDMX v6 / LuxDMX-4uni boards. It implements a modular 5-layer architecture wired together by a thin `main.cpp` entry point.

### 5-Layer Model

| Layer | Directory | Responsibility |
|---|---|---|
| **drv** (drivers) | `src/drv/` | Low-level hardware: RMT DMX transmit, UART RX for RDM, DE/RE GPIO |
| **cfg** (config) | `src/cfg/` | Schema-driven config engine: single field table drives NVS, serial console, web form |
| **core** (DMX/RDM) | `src/core/` | DMX merge engine, seqlock buffer, sender tracking, RDM E1.20 controller, scene engine |
| **net** (network) | `src/net/` | Self-implemented Art-Net/sACN, AsyncWebServer, WebSocket, W5500/RMII Ethernet, OTA (Ed25519 signed) |
| **sys/app** (system) | `src/sys/` | FreeRTOS tasks (core-pinned), LED/display drivers, crash-guard, soak monitor, syslog |

### Core Pinning

- **Core 0**: `netRxTask` (priority 5), WiFi/lwIP stack, AsyncTCP (pinned via `CONFIG_ASYNC_TCP_RUNNING_CORE=0`), web server, serial console
- **Core 1**: `dmxTxTask` (priority 19), RMT DMX transmit, RDM service, merge engine

This isolation prevents AsyncTCP from preempting the RDM timing path. Measured on the HIL bench (see `platformio.ini:42-45`): with AsyncTCP on core 1 at priority 10 it preempts `loop()`, inflating the RDM controller TX→RX turnaround 3-4× (median 15→53 µs, max 126 µs).

### Concurrency Primitives

| Primitive | File | Used By |
|---|---|---|
| `SeqLock` | `include/seqlock.h` | `dmx_buffer.cpp` — single-writer (netRxTask, core 0) / single-reader (dmxTxTask, core 1) |
| FreeRTOS task | `src/sys/tasks.cpp` | `dmxTxTask` (core 1, prio 19), `netRxTask` (core 0, prio 5), `ledTask`, `displayTask`, `versionCheckTask` |
| SPSC ring (volatile + `__sync_synchronize`) | `src/net/art_rdm_resp_queue.cpp` | Cross-core Art-Net RDM response relay (8×260 B, `volatile` + `__sync_synchronize`) |
| NVS | `Preferences.h` | Config persistence, crash guard counter |

## 2. Architecture Layer Map

### drv (Drivers)

| Module | Link |
|---|---|
| DMX RMT TX | [drv-dmx-rmt-tx](./drv-dmx-rmt-tx.md) |
| DMX UART RX | [drv-dmx-uart-rx](./drv-dmx-uart-rx.md) |
| GPIO DIR | [drv-gpio-dir](./drv-gpio-dir.md) |

### cfg (Config Engine)

| Module | Link |
|---|---|
| Config Engine | [config-engine](./config-engine.md) |

### core (DMX/RDM)

| Module | Link |
|---|---|
| DMX Buffer | [core-dmx-buffer](./core-dmx-buffer.md) |
| Merge Engine | [core-merge-engine](./core-merge-engine.md) |
| Sender Tracker | [core-sender-tracker](./core-sender-tracker.md) |
| Frame Router | [core-frame-router](./core-frame-router.md) |
| Scene Engine | [core-scene-engine](./core-scene-engine.md) |
| Output Init | [core-output-init](./core-output-init.md) |
| RDM Engine | [core-rdm-engine](./core-rdm-engine.md) |
| RDM Discovery | [core-rdm-discovery](./core-rdm-discovery.md) |
| RDM Task | [core-rdm-task](./core-rdm-task.md) |
| RDM Typed | [core-rdm-typed](./core-rdm-typed.md) |
| Input Router | [core-input-router](./core-input-router.md) |
| Stats | [core-stats](./core-stats.md) |

### net (Network)

| Module | Link |
|---|---|
| Art-Net Protocol | [net-artnet-protocol](./net-artnet-protocol.md) |
| sACN Protocol | [net-sacn-protocol](./net-sacn-protocol.md) |
| ArtNet Bridge | [net-artnet-bridge](./net-artnet-bridge.md) |
| Art Pkt Queue | [net-art-pkt-queue](./net-art-pkt-queue.md) |
| Art RDM Resp Queue | [net-art-rdm-resp-queue](./net-art-rdm-resp-queue.md) |
| sACN Pkt Queue | [net-sacn-pkt-queue](./net-sacn-pkt-queue.md) |
| WebSocket Protocol | [net-websocket-protocol](./net-websocket-protocol.md) |
| WebSocket Handler | [net-websocket-handler](./net-websocket-handler.md) |
| Web Server | [net-web-server](./net-web-server.md) |
| Web Routes | [net-web-routes](./net-web-routes.md) |
| Web Frontend | [net-web-frontend](./net-web-frontend.md) |
| Rate Limiter | [net-rate-limiter](./net-rate-limiter.md) |
| OTA | [net-ota](./net-ota.md) |
| OTA Sign | [net-ota-sign](./net-ota-sign.md) |
| Ethernet | [net-ethernet](./net-ethernet.md) |
| Net State | [net-net-state](./net-net-state.md) |
| Setup Portal | [net-setup-portal](./net-setup-portal.md) |

### sys (System)

| Module | Link |
|---|---|
| Tasks | [sys-tasks](./sys-tasks.md) |
| Crash Guard | [sys-crash-guard](./sys-crash-guard.md) |
| LED Status | [sys-led-status](./sys-led-status.md) |
| Soak Monitor | [sys-soak-monitor](./sys-soak-monitor.md) |
| Firmware Version | [sys-firmware-version](./sys-firmware-version.md) |
| Syslog | [sys-syslog](./sys-syslog.md) |
| Alert | [sys-alert](./sys-alert.md) |

### app (Application)

| Module | Link |
|---|---|
| Enc Decode | [app-enc-decode](./app-enc-decode.md) |
| Input Map | [app-input-map](./app-input-map.md) |
| Menu | [app-menu](./app-menu.md) |

### Include Headers

| Module | Link |
|---|---|
| Include Headers | [include-headers](./include-headers.md) |

### Supporting Modules

| Module | Link |
|---|---|
| Build System | [build-system](./build-system.md) |
| Architecture Overview | [architecture-overview](./architecture-overview.md) |
| Test Infrastructure | [test-infrastructure](./test-infrastructure.md) |

## 3. Data Flow Summary

### Network Input → DMX Output

1. `netRxTask` (core 0) drains UDP on Art-Net (6454) and sACN (5568) sockets — bounded to 64 packets/call for Art-Net and 4/socket for sACN (`src/sys/tasks.cpp:149-152`)
2. Incoming frames are staged into `dmxBuffers[i].data[]` via the **seqlock** (`include/seqlock.h`). The writer bumps a 32-bit ticket (odd = mid-write); the reader (core 1) takes a `memcpy` and retries if the ticket moved or was odd
3. `dmxTxTask` (core 1) calls `snapshotAndTransmit()` every 1 ms tick — snapshots the buffer via seqlock, then kicks the RMT peripheral (`src/sys/tasks.cpp:96-108`)

### Art-Net RDM Async Response Path (Cross-Core)

1. Core 0 `netRxTask` receives packets → `handleArtRdm` → `rdmArtRawRelayEnqueue` (non-blocking `xQueueSend` with `lineIdx`) → core 1 `RDMTask` receives cmd → `rdmRmtSelect(cmd.lineIdx)` → `rdmTx` → `rdmReadFrame` → `artRdmRespEnqueue` (`src/core/rdm_task.cpp:295`, `include/rdm_types.h`)

## 4. Configuration Resolution

Resolution order: neutral value (from constraint) → active board template → saved NVS value (`src/cfg/config_core.cpp:167-211`).

Board defaults live in `templates/*.ini`. The active template is selected by `DEFAULT_TEMPLATE` (set per-env in `platformio.ini`). Generated template text is embedded at build time by `tools/gen_config_templates.py` (invoked by `extra_scripts.py`) into `src/generated/config_templates.gen.h`.

## 5. Build Gate

Every documentation change must preserve build correctness: `pio run -e esp32s3_psram` must remain green. Documentation files in `docs/` do not affect the build.
