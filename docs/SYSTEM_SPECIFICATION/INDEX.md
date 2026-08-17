# System Specification Index — LuxDMX-v2

## 1. System Overview

This document is the master index for the **LuxDMX-v2 Black-Box System Specification** — a technology-agnostic description of the firmware's structure, behavior, and interfaces. It abstracts away all implementation details (file names, class names, line numbers) and describes the system in terms of **layers**, **components**, **data flows**, and **protocol boundaries**.

The firmware is an Art-Net (E1.19) / sACN (E1.31) gateway that converts network DMX data to physical DMX512-A output, with full RDM (E1.20) controller support, OTA updates, WebSocket status, and a web configuration interface.

### 5-Layer Component Model

```
drivers  →  config  →  core  →  network  →  system
```

| Layer | Components | Responsibility |
|---|---|---|
| **Drivers** | DMX Transmit, DMX Receive, GPIO Direction | Direct hardware: RMT DMX transmit, UART RX for RDM, DE/RE GPIO |
| **Config** | Config Engine | Schema-driven configuration: single field table drives persistence (NVS), serial console, web form |
| **Core** | DMX Buffer, Merge Engine, Sender Tracker, Frame Router, Scene Engine, Output Init, RDM Engine, RDM Discovery, RDM Task, Stats | DMX frame storage, source merging, sender tracking, frame routing, scene playback, output lifecycle, RDM transport, RDM discovery, statistics |
| **Network** | Art-Net, sACN, WebSocket, Ethernet, Web Server, OTA, Rate Limiter, OTA Sign | Protocol reception and dispatch, web interface, firmware updates |
| **System** | Tasks, LED, Display, Crash Guard, Soak Monitor, Firmware Version, Syslog, Alert | FreeRTOS task scheduling, status indicators, crash recovery, diagnostics, remote logging, webhook alerts |

### Core Isolation Strategy

The firmware executes on a dual-core processor with strict core affinity:

- **Core 0**: Network receive at priority 5, WiFi/lwIP stack, web server, serial console, all network protocol parsing.
- **Core 1**: DMX transmit and RDM at priority 19, RMT peripheral control, merge engine, RDM task (priority 18).

This isolation ensures that network activity — including WiFi DMA contention and web-server request processing — cannot preempt or corrupt the time-critical DMX transmission path. The RMT peripheral clocks DMX breaks and marks autonomously in hardware, so the CPU is not on the bit-timing path.

### Cross-Core Primitives

| Primitive | Purpose | Producer (Core) | Consumer (Core) |
|---|---|---|---|
| SeqLock | Tear-free 513-byte DMX frame buffer copy | Core 0 (network receive) | Core 1 (DMX transmit) |
| SPSC Response Ring | Cross-core RDM response relay | Core 1 (RDM task) | Core 0 (network transmit) |

## 2. Data Flow Summary

### Network Input to DMX Output

1. The **Network Receive** component (Core 0) drains UDP packets from the Art-Net socket (port 6454) and sACN multicast socket (port 5568), bounded to a fixed packet budget per 2 ms cycle.
2. Incoming DMX frames are decoded (universe, priority, slot data) and routed to matching outputs via the **Frame Router**.
3. The **Sender Tracker** caches each source's most recent frame, priority, and activity timestamp.
4. The **Merge Engine** selects contributing senders based on the configured merge mode and E1.31 priority, producing a merged 513-byte frame.
5. The merged frame is written to the per-output **DMX Buffer** behind a seqlock (technique: 32-bit ticket, odd = mid-write).
6. The **DMX Transmit** task (Core 1, 1 ms tick) snapshots the buffer via the seqlock reader, then triggers the RMT peripheral to transmit the frame.
7. The RMT peripheral clocks out break, mark-after-break, start code, and 512 slots in hardware at 250 kbaud.

### Art-Net RDM Async Response Path (Cross-Core)

1. Core 0 receives an Art-Net RDM packet and enqueues it to the core-1 RDM task via a non-blocking queue.
2. The RDM task (Core 1) selects the target output's RMT channel and DE/RE GPIO, transmits the RDM request via RMT, then switches the UART to receive mode.
3. The RDM response is read on a dedicated RX-only UART (never shared with DMX input).
4. The response is enqueued to the cross-core response ring.
5. Core 0 drains the ring and sends the ArtRdm reply via UDP back to the requesting controller.

### Configuration Resolution

1. **Neutral values** are applied from structural constraints (e.g., pin ranges, enum bounds).
2. **Board template** defaults are resolved from the active template (selected at build time per target board).
3. **Saved NVS values** overlay on top of the template, with clamping to field-defined min/max ranges.

## 3. Protocol Summary

| Protocol | Transport | Port | Description |
|---|---|---|---|
| Art-Net 4 | UDP | 6454 | Opcodes: Poll, PollReply, DMX, Sync, Address, RDM, TimeCode, Trigger |
| sACN (E1.31) | UDP Multicast | 5568 | Stream, StreamSync, Universe Discovery |
| WebSocket | TCP | 80 (WSS) | Binary status/DMX frame push (~10 Hz) + JSON commands |
| HTTP/HTTPS | TCP | 80 (HTTP), 443 (HTTPS) | Web UI, config, OTA, diagnostics |
| Syslog (RFC 5424) | UDP | Configurable (default 514) | Remote logging |

## 4. Configuration Model

All configurable parameters are described by a single **field table** (the schema). Each field carries:

- A key name and JSON export name.
- A type (integer, boolean, string, enum).
- A min/max range.
- A human-readable label and UI group.
- **Live vs. reboot semantics**: Live fields apply instantly on save; reboot fields require a restart.
- Optional flags: secret (masked in dumps), read-only, hidden from web form, keep-if-not-empty.

Settings are resolved at boot in order: neutral value -> board template -> saved NVS value.

## 5. Build Gate

Every documented behavior is grounded in the reference implementation. The canonical build environment is the **ESP32-S3 with 8 MB PSRAM** target. The build MUST remain green:

```
pio run -e esp32s3_psram
```

## 6. Specification Plan

The following specification files have been produced (in order):

| # | File | Domain |
|---|---|---|
| 01 | `01-system-architecture-spec.md` | system.architecture |
| 02 | `02-config-engine-spec.md` | cfg.config-engine |
| 03 | `03-dmx-buffer-spec.md` | core.dmx-buffer |
| 04 | `04-merge-engine-spec.md` | core.merge-engine |
| 05 | `05-sender-tracker-spec.md` | core.sender-tracker |
| 06 | `06-frame-router-spec.md` | core.frame-router |
| 07 | `07-output-init-spec.md` | core.output-init |
| 08 | `08-scene-engine-spec.md` | core.scene-engine |
| 09 | `09-rdm-engine-spec.md` | core.rdm-engine |
| 10 | `10-rdm-discovery-spec.md` | core.rdm-discovery |
| 11 | `11-rdm-task-spec.md` | core.rdm-task |
| 12 | `12-input-router-spec.md` | core.input-router |
| 13 | `13-stats-spec.md` | core.stats |
| 14 | `14-dmx-rmt-tx-spec.md` | drv.dmx-rmt-tx |
| 15 | `15-dmx-uart-rx-spec.md` | drv.dmx-uart-rx |
| 16 | `16-gpio-dir-spec.md` | drv.gpio-dir |
| 17 | `17-artnet-protocol-spec.md` | net.artnet |
| 18 | `18-sacn-protocol-spec.md` | net.sacn |
| 19 | `19-artnet-bridge-spec.md` | net.artnet-bridge |
| 20 | `20-art-packet-queue-spec.md` | net.art-pkt-queue |
| 21 | `21-art-rdm-resp-queue-spec.md` | net.art-rdm-resp-queue |
| 22 | `22-sacn-packet-queue-spec.md` | net.sacn-pkt-queue |
| 23 | `23-websocket-protocol-spec.md` | net.websocket |
| 24 | `24-websocket-handler-spec.md` | net.websocket-handler |
| 25 | `25-web-server-spec.md` | net.web-server |
| 26 | `26-web-routes-spec.md` | net.web-routes |
| 27 | `27-web-frontend-spec.md` | net.web-frontend |
| 28 | `28-rate-limiter-spec.md` | net.rate-limiter |
| 29 | `29-ota-spec.md` | net.ota |
| 30 | `30-ota-sign-spec.md` | net.ota-sign |
| 31 | `31-ethernet-spec.md` | net.ethernet |
| 32 | `32-net-state-spec.md` | net.net-state |
| 33 | `33-setup-portal-spec.md` | net.setup-portal |
| 34 | `34-tasks-spec.md` | sys.tasks |
| 35 | `35-crash-guard-spec.md` | sys.crash-guard |
| 36 | `36-led-status-spec.md` | sys.led-status |
| 37 | `37-display-spec.md` | sys.display |
| 38 | `38-soak-monitor-spec.md` | sys.soak-monitor |
| 39 | `39-firmware-version-spec.md` | sys.firmware-version |
| 40 | `40-syslog-spec.md` | sys.syslog |
| 41 | `41-alert-spec.md` | sys.alert |
| 42 | `42-enc-decode-spec.md` | app.enc-decode |
| 43 | `43-input-map-spec.md` | app.input-map |
| 44 | `44-menu-spec.md` | app.menu |
| 45 | `45-include-headers-spec.md` | include (cross-cutting) |
| 46 | `46-build-system-spec.md` | system.build |
| 47 | `47-test-infrastructure-spec.md` | system.test |
All 47 specification files are complete and verified present on disk.
## 7. Legend

- **MUST**: An absolute requirement of the specification.
- **MUST NOT**: An absolute prohibition.
- **SHOULD**: There is a valid reason to do this in a given context, but not doing it may be acceptable.
- **SHOULD NOT**: There is a valid reason not to do this in a given context.
- **MAY**: One may or may not do this; the implementer decides based on context.
- **RECOMMENDED**: A course of action that, if followed, has measurable benefits.
