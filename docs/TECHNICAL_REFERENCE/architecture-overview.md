Architecture Overview — Technical Reference

Domain: `system.architecture`

## 1. Domain Scope

This document provides a consolidated view of the LuxDMX-v2 firmware architecture: the 5-layer model, core pinning strategy, data flow between layers, and the critical timing isolation that prevents network traffic from corrupting DMX/RDM signals. It cross-references the per-module deep-dive documents in this directory.

The firmware is a thin `main.cpp` (166 lines) that wires together five architectural layers in a numbered setup sequence. No business logic lives in `main.cpp` — it is call-sequence glue between layer init and task spawn.

## 2. Layer Mapping

```
drv  →  cfg  →  core  →  net  →  app/sys   (wired by main.cpp)
```

| Layer | Directory | Responsibility | Key Modules |
|---|---|---|---|
| **drv** (drivers) | `src/drv/` | Low-level hardware: RMT DMX TX, UART RX for RDM, DE/RE GPIO | [DMX RMT TX](./drv-dmx-rmt-tx.md), [DMX UART RX](./drv-dmx-uart-rx.md), [GPIO DIR](./drv-gpio-dir.md) |
| **cfg** (config) | `src/cfg/` | Schema-driven config: single field table drives NVS, serial console, web form | [Config Engine](./config-engine.md) |
| **core** (DMX/RDM) | `src/core/` | DMX merge engine, seqlock buffer, sender tracking, RDM E1.20, scene engine | [DMX Buffer](./core-dmx-buffer.md), [Merge Engine](./core-merge-engine.md), [Sender Tracker](./core-sender-tracker.md), [Frame Router](./core-frame-router.md), [Scene Engine](./core-scene-engine.md), [Output Init](./core-output-init.md), [RDM Engine](./core-rdm-engine.md), [RDM Discovery](./core-rdm-discovery.md), [RDM Task](./core-rdm-task.md), [RDM Typed](./core-rdm-typed.md), [Input Router](./core-input-router.md), [Stats](./core-stats.md) |
| **net** (network) | `src/net/` | Self-implemented Art-Net/sACN, web server, WebSocket, Ethernet, OTA | [Art-Net Protocol](./net-artnet-protocol.md), [sACN Protocol](./net-sacn-protocol.md), [ArtNet Bridge](./net-artnet-bridge.md), [Art Packet Queue](./net-art-pkt-queue.md), [Art RDM Resp Queue](./net-art-rdm-resp-queue.md), [sACN Packet Queue](./net-sacn-pkt-queue.md), [WebSocket Protocol](./net-websocket-protocol.md), [WebSocket Handler](./net-websocket-handler.md), [Web Server](./net-web-server.md), [Web Routes](./net-web-routes.md), [Web Frontend](./net-web-frontend.md), [Rate Limiter](./net-rate-limiter.md), [OTA](./net-ota.md), [OTA Sign](./net-ota-sign.md), [Ethernet](./net-ethernet.md), [Net State](./net-net-state.md), [Setup Portal](./net-setup-portal.md) |
| **sys/app** (system) | `src/sys/`, `src/app/` | FreeRTOS tasks, LED/display, crash guard, soak monitor, syslog, OTA version check | [Tasks](./sys-tasks.md), [Crash Guard](./sys-crash-guard.md), [LED Status](./sys-led-status.md), [Soak Monitor](./sys-soak-monitor.md), [Firmware Version](./sys-firmware-version.md), [Syslog](./sys-syslog.md), [Alert](./sys-alert.md), [Enc Decode](./app-enc-decode.md), [Input Map](./app-input-map.md), [Menu](./app-menu.md) |

## 3. Source Tree Layout

```
src/
├── main.cpp                # 166-line thin wiring layer (setup + loop)
├── config_templates_gen.cpp # Wraps generated/config_templates.gen.h
├── cfg/
│   ├── config_core.cpp/h   # load/save/setValue/getValue, template resolution
│   ├── config_schema.cpp   # CONFIG_FIELDS[] + OUTPUT_FIELDS[] table (THE source of truth)
│   ├── config_serial.cpp/h # Serial console grammar (dump/set/get/save/wifi/reboot)
│   └── nvs_migrate.cpp/h   # Legacy NVS key migration (o0_* → a_*, apfb → fbmode)
├── drv/
│   ├── dmx_rmt.h           # RMT-based DMX512 TX (issue #64)
│   ├── dmx_input.cpp/h     # DMX input framing (BRK_DET + inter-byte timeout)
│   ├── uart_rx.h           # RX-only UART primitive for RDM responses
│   └── gpio_dir.h          # DE/RE GPIO direction control
├── core/
│   ├── dmx_buffer.cpp/h    # Seqlock-protected per-output DMX frame buffers
│   ├── merge_engine.cpp/h  # 5 merge modes, 5 loss modes, failsafe timeouts
│   ├── sender_tracker.cpp/h # 16-slot sender table, eviction, status queries
│   ├── frame_router.cpp/h  # Universe routing, splitMask mirroring
│   ├── scene_engine.cpp/h  # Scene presets, fade engine, timecode triggers
│   ├── output_init.cpp/h   # RMT init, crash-guard wiring, RDM line mapping
│   ├── rdm_engine.cpp/h    # RMT-TX + UART-RX RDM transport
│   ├── rdm_disc.cpp/h      # DISC_UNIQUE_BRANCH binary search
│   ├── rdm_task.cpp/h      # Core-1 RDM task + command queue
│   ├── rdm_typed.cpp       # Typed PID wrappers
│   ├── input_router.cpp/h  # DMX-in → network bridge poller
│   └── stats.cpp/h         # StatsState singleton (fps, heap, senders)
├── net/
│   ├── artnet.cpp/h        # Art-Net UDP socket, packet parsing, dispatch
│   ├── artnet_bridge.cpp   # Control opcode handlers (poll, address, RDM)
│   ├── art_pkt_queue.cpp/h # SPSC ring: 8 packets/tick bounded receive
│   ├── art_rdm_resp_queue.cpp/h # Cross-core RDM response ring (8×260B)
│   ├── sacn.cpp/h          # sACN E1.31 multicast receive
│   ├── sacn_pkt_queue.cpp/h # SPSC ring for sACN packets
│   ├── ws_frame.cpp/h      # WebSocket binary frame builder
│   ├── websocket.cpp/h     # AsyncWebSocket instance + push logic
│   ├── ws_handler.cpp/h    # JSON command dispatch
│   ├── web_server.cpp/h    # AsyncWebServer routes + rate limiter wrapper
│   ├── web_routes.cpp/h    # Dynamic route handlers (config, OTA, RDM)
│   ├── web_pages.cpp/h     # Static asset handlers (logo, favicon, CSS)
│   ├── rate_limiter.cpp/h  # Token-bucket IP limiter (OTA 5/min, config 30/min)
│   ├── ota.cpp/h           # OTA pipeline (GitHub, URL, upload)
│   ├── ota_sign.cpp/h      # Ed25519 signature verification
│   ├── ethernet.cpp/h      # W5500 SPI + RMII LAN8720 bring-up
│   ├── net_state.cpp/h     # WiFi/Ethernet interface abstraction
│   └── setup_portal.cpp    # Captive DNS portal
├── sys/
│   ├── tasks.cpp/h         # Task creation, dmxTxTask, netRxTask
│   ├── led_status.cpp/h    # LED color/brightness (plain/WLED/WS2812/panel)
│   ├── display.cpp/h       # OLED/SPI display (stub)
│   ├── soak_monitor.cpp/h  # LUXDMX_SOAK_TEST heap watchdog
│   ├── firmware_version.cpp/h # Version string, BOARD_ID, version check
│   ├── syslog.cpp/h        # RFC 5424 UDP syslog client
│   ├── alert.cpp/h         # Webhook alerts on source loss
│   └── sys_platform.cpp/h  # Platform detection stubs
├── frontend/
│   ├── web_frontend.cpp/h  # PROGMEM HTML assembly
│   ├── base/*.h            # Navbar, footer, icons, styles
│   ├── pages/*.h           # Config, index, RDM, OTA, setup, reset pages
│   └── scripts/*.h         # Client-side JS (config, index, RDM, shared)
├── pages/*.html            # Source HTML templates
├── assets/*                # Static assets (logo.webp, favicon.png, bootstrap.min.css)
└── generated/*.h           # Auto-generated PROGMEM headers (build-time)
include/
├── config_schema.h         # Config/DmxOutput structs, MAX_OUTPUTS
├── config_types.h          # CfgField/CfgOutputField descriptors
├── config_enums.h          # Merge/loss/Mode enums
├── output.h                # dmx_output_t, output_mode_t, RmtDmx
├── seqlock.h               # Generic SPSC seqlock (issue #64)
├── rdm_types.h             # RDM UID, PID, ack structs (E1.20)
└── eth_phy.h               # RMII PHY name table + type mapping
templates/*.ini             # Per-board runtime defaults
test/
├── native/                 # Host smoke tests (no framework)
│   ├── config_test.cpp     # Template resolution, setValue/getValue, NVS round-trip
│   ├── seqlock_test.cpp    # Seqlock snapshot/retry
│   ├── merge_test.cpp      # HTP, LTP, LOSS_ZERO, priority merge
│   ├── rdm_types_test.cpp  # UID enums, PID constants
│   ├── shim/               # Arduino/ESP IDP shims for MSVC
│   └── run_all.sh/bat      # Test runner wrappers
└── unit-test/              # Unity framework unit tests
    ├── test_config/        # cfgcore + cfgserial unit tests
    ├── test_merge/         # Merge engine unit tests
    ├── test_rdm_types/     # RDM type/constant unit tests
    └── test_seqlock/       # Seqlock unit tests
```

## 4. Data Structures

The architecture is defined by a small set of shared data structures:

| Struct | File | Owner | Description |
|---|---|---|---|
| `Config` | `include/config_schema.h` | cfg | Global config singleton — all persisted settings |
| `DmxOutput` | `include/config_schema.h` | cfg | Per-output config (pins, universe, modes) |
| `CfgField` / `CfgOutputField` | `include/config_types.h` | cfg | Schema descriptor table entries |
| `SeqLock` | `include/seqlock.h` | drv/core | 32-bit ticket lock for cross-core buffer |
| `DmxBuffer` / `DmxBufferState` | `src/core/dmx_buffer.h` | core | Per-output DMX frame + seqlock + staged buffers |
| `RmtDmx` | `src/drv/dmx_rmt.h` | drv | RMT TX channel state + byte LUT |
| `dmx_output_t` | `include/output.h` | core | Runtime output state (RmtDmx + UART + DE/RE) |
| `ArtPkt` | `src/net/art_pkt_queue.h` | net | Parsed Art-Net packet for SPSC ring |
| `ArtRdmResp` | `src/net/art_rdm_resp_queue.h` | net | RDM response for cross-core ring |
| `SacnPkt` | `src/net/sacn_pkt_queue.h` | net | Parsed sACN packet for SPSC ring |
| `Sender` | `src/core/sender_tracker.h` | core | Active source tracking entry |
| `RdmCmd` | `src/core/rdm_task.h` | core | RDM task command (transaction/discover/mute/relay) |
| `ArtNetState` | `src/net/artnet.h` | net | Global Art-Net state |
| `StatsState` | `src/core/stats.h` | core | Live stats (fps, heap, senders, RDM counts) |
| `RateLimitEntry` | `src/net/rate_limiter.h` | net | Per-IP token bucket entry |

## 5. Concurrency Model

### Task Table (from `src/sys/tasks.cpp:78-85`)

| Task | Core | Priority | Stack | Period |
|---|---|---|---|---|
| `dmxTxTask` | Core 1 | 19 | 8192 | 1 ms |
| `netRxTask` | Core 0 | 5 | 8192 | 2 ms |
| `ledTask` | Any | 1 | 2048 | 50 ms |
| `displayTask` | Any | 1 | 4096 | 200 ms |
| `versionCheckTask` | Any | 1 | 12288 | 60 s |

### Core Isolation Strategy

The critical design decision is core 0 vs core 1 isolation:

- **Core 0** hosts: WiFi/lwIP stack, AsyncTCP (pinned via `CONFIG_ASYNC_TCP_RUNNING_CORE=0`), AsyncWebServer, `netRxTask`, `loop()` (serial console, web push), sACN/Art-Net UDP receive.
- **Core 1** hosts: `dmxTxTask` (priority 19), RMT DMX transmit, RDM task (priority 18), merge engine, seqlock reader.

The AsyncTCP hardening block in `platformio.ini:46-50` pins AsyncTCP to core 0 specifically to prevent it from preempting the 1 ms DMX ticker on core 1. Measured on the HIL bench: with AsyncTCP on core 1 at priority 10, it preempts `loop()`, inflating the RDM controller TX→RX turnaround 3-4× (median 15→53 µs, max 126 µs) (`platformio.ini:42-45`).

### Cross-Core Primitives

| Primitive | File | Producer | Consumer |
|---|---|---|---|
| `SeqLock` | `include/seqlock.h` | Core 0 (merge output) | Core 1 (dmxTxTask snapshot) |
| `ArtRdmResp` ring | `src/net/art_rdm_resp_queue.cpp` | Core 1 (RDM task) | Core 0 (netRxTask drain) |
| `ArtPkt` ring | `src/net/art_pkt_queue.cpp` | Core 0 (artRdmPollRx) | Core 0 (artPktDispatchAll) — same core, lock-free as defensive |
| `SacnPkt` ring | `src/net/sacn_pkt_queue.cpp` | Core 0 (readSacnSocket) | Core 0 (readSacn) — same core |

## 6. State Machine

The firmware has no single global state machine. Instead, each subsystem has its own state:

- **Config Engine:** Neutral → Template → NVS overlay (resolution order)
- **Network:** Boot → WiFi Station/AP/Ethernet → Connected → (Setup Portal on failure)
- **DMX TX:** Idle → Snapshot → RMT Encode → RMT Transmit → Idle (1 ms loop)
- **RDM Task:** Idle → Command Queue → RMT TX → UART RX (9 ms) → Response → Idle
- **OTA:** Idle → Phase 1 (Downloading) → Phase 2 (Verifying/Sending) → Phase 3 (Error)
- **Scene Engine:** Idle → Fade (1 ms step) → Idle
- **Link Loss:** Configured → Link Lost → Policy Applied (Retry/AP/Reboot/WiFi)

## 7. Entry Points

The firmware has two entry points:

### `setup()` (main.cpp:38-131)

1. Serial begin + stats start: `main.cpp:39-42`
2. NVS migration + config load: `main.cpp:45-47`
3. Serial config console: `main.cpp:50`
4. LED + display init: `main.cpp:53-55`
5. Network bring-up (Ethernet/AP/Station): `main.cpp:58-75`
6. OTA boot update + mDNS: `main.cpp:78-103`
7. Output init (RMT, UART RX, DE/RE): `main.cpp:106-112`
8. RDM task init: `main.cpp:115`
9. Network protocol init (Art-Net, sACN): `main.cpp:118-122`
10. Web server + WebSocket: `main.cpp:125-127`
11. Task spawn: `main.cpp:130-131`

### `loop()` (main.cpp:133-166)

- Serial config console poll: `main.cpp:135`
- Setup portal DNS pump: `main.cpp:138`
- Dirty-flag persistence: `main.cpp:141-147`
- WS RDM queue process: `main.cpp:150`
- Input router poll: `main.cpp:153`
- WebSocket live push (10 Hz) + meta (2 Hz): `main.cpp:156-165`

## 8. Data Flow

### Network Input → DMX Output

1. `netRxTask` (core 0) drains UDP on Art-Net (6454) and sACN (5568) sockets — bounded to 8 packets/call for Art-Net and 4/socket for sACN (`tasks.cpp:149-151`)
2. Incoming ArtDMX frames: `artHandlePacket` extracts universe+data → `routeFrame(worldUniverse, data, length, ip, proto=0, priority)` → writes to `dmxBufWriteBegin/End` with seqlock (`artnet.cpp:171-190`)
3. Incoming sACN frames: `readSacnSocket` → `routeFrame(universe, data, len, ip, proto=2, priority)` → seqlock write (`sacn.cpp`)
4. `dmxBufWriteBegin` bumps the seqlock ticket (odd = mid-write) (`dmx_buffer.h:32`)
5. `dmxTxTask` (core 1) calls `snapshotAndTransmit()` every 1 ms (`tasks.cpp:120-136`):
   - `dmxBufSnapshot` takes a consistent `memcpy` under seqlock retry (`dmx_buffer.h:44`)
   - `rmtDmxIdle` checks if RMT is free (`dmx_rmt.h:190`)
   - `rmtDmxKick` encodes + transmits the frame (`dmx_rmt.h:172`)

### Art-Net RDM Async Response Path (Cross-Core)

1. Core 0 `netRxTask` receives ArtRdm packet → `handleArtRdm` → `rdmArtRawRelayEnqueue(req, len, ip, line)` (non-blocking `xQueueSend` with `lineIdx`) → core 1 RDM task receives cmd → `rdmRmtSelect(cmd.lineIdx)` → `rdmTx` → `rdmReadFrame` → `artRdmPushResponse(resp)` → core 0 `artRdmDrainResponses` sends via UDP 6454 → requester

   Source: `artnet_bridge.cpp:132-149`, `rdm_task.h:98`, `tasks.cpp:149-154`

### Configuration Resolution

1. `resetToTemplate()` → `applyNeutral()` → `applyTemplate(DEFAULT_TEMPLATE)` (`config_core.cpp:157-160`)
2. `load()` → apply NVS overlay on top of template (`config_core.cpp:167-211`)
3. Resolution order: neutral → template → NVS (`config_core.cpp:38-39`)

## 9. Protocol Layout

The firmware implements two network protocols:

| Protocol | Port | Module | Details |
|---|---|---|---|
| Art-Net 4 | UDP 6454 | [net-artnet-protocol](./net-artnet-protocol.md) | Opcodes: POLL, POLLREPLY, DMX, SYNC, ADDRESS, IPROG, RDM, TIMECODE, TRIGGER |
| sACN (E1.31) | UDP 5568 | [net-sacn-protocol](./net-sacn-protocol.md) | Multicast, Stream/StreamSync/Discovery vectors |
| WebSocket | TCP 80 (WSS) | [net-websocket-protocol](./net-websocket-protocol.md) | Binary 2095-byte frame, JSON text commands |

## 10. Configuration Integration

All configurable settings flow through the `Config` struct (`include/config_schema.h:38`) and the `CONFIG_FIELDS[]` + `OUTPUT_FIELDS[]` tables (`config_schema.cpp:46-132,153-178`). Every persisted setting is one row. Live vs. reboot flags determine whether a change applies instantly or requires restart:

- `CFG_LIVE` fields (web UI / serial): universe, net, subnet, TX rate, TX style, LED brightness, merge mode, loss mode, protocol, timecode, syslog, webhook, RDM, DSCP
- `CFG_REBOOT` fields: LED pin/type, pins, UART port, network mode, display config, controls, VLAN, Ethernet chip select/MDIO/MDC

## 11. Lifecycle

The firmware has a single well-defined lifecycle:

1. **NVS migration** — one-shot V1→V2 key migration (`nvs_migrate.cpp:14`)
2. **Config load** — template + NVS overlay (`config_core.cpp:167`)
3. **Sanitize outputs** — clamp/validate per-output pin sets (not determinable — function not found in inspected source)
4. **Network bring-up** — WiFi AP/STA or Ethernet (`main.cpp:58-75`)
5. **OTA boot update** — resume deferred OTA (`ota.cpp:19`)
6. **mDNS** — register HTTP + Art-Net + sACN services (`main.cpp:79-103`)
7. **Output init** — crash-guard begin, RMT init, UART RX, DE/RE GPIO (`main.cpp:106-112`)
8. **RDM task init** — core 1 task + queue (`main.cpp:115`)
9. **Protocol init** — ArtNet, sACN (`main.cpp:118-122`)
10. **Web server + WS** — routes + WebSocket init (`main.cpp:125-127`)
11. **Task spawn** — create FreeRTOS tasks (`main.cpp:130-131`)
12. **Loop** — serial console, DNS pump, dirty flags, WS push, input router (`main.cpp:133-166`)

## 12. Error Handling

| Subsystem | Mechanism | Source |
|---|---|---|
| Config | `esp_err_t` return + `String& err` out-param | `config_core.h:11-12` |
| Art-Net | Socket init check, `artSock >= 0` guard | `artnet.cpp:41-58` |
| Ethernet | 15 s link timeout, 30 s bring-up timeout | `ethernet.cpp:13,67,98` |
| Crash Guard | NVS-backed counter, progressive output disable | `tasks.cpp:43-76` |
| OTA | `bootTry` counter, auto-revert on signature failure | `ota.cpp:19-41, ota_sign.cpp:119-123` |
| RDM | `rdmReadResp` returns bool, 9 ms timeout | `rdm_engine.h:19,68` |
| seqlock | 8 retry attempts, snapshot returns `false` | `seqlock.h:21-30` |

## 13. Memory Allocation

- **DRAM (internal):** RMT symbol buffers (`heap_caps_malloc` with `MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA`), `dmxBufSnapshot` frame copies, task stacks (8-12 KB each)
- **PSRAM (external):** Scene array (luxdmx_4uni template, PSRAM-enabled builds), WiFi/lwIP buffers (`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`)
- **Static/BSS:** All ring buffers (`artRing[32]`, `rdmRespRing[8]`, `sacnRing[16]`), global config struct, LED state, sender table
- **Stack:** `dmxTxTask` 8192, `netRxTask` 8192, `versionCheckTask` 12288, `ledTask` 2048, `displayTask` 4096
- **PROGMEM:** Gzipped HTML pages, CSS, images, config templates (build-time generated)

## 14. Timing

| Path | Period | Deadline | Source |
|---|---|---|---|
| `dmxTxTask` tick | 1 ms | 44 µs/frame (23 ms/frame at 44 Hz) | `tasks.cpp:138-144` |
| `netRxTask` tick | 2 ms | All UDP recv + dispatch within 2 ms | `tasks.cpp:146-155` |
| Art-Net recv | Bounded 8 packets/tick | 2 ms budget | `artnet.cpp:68` |
| sACN recv | Bounded 4 packets/socket | 2 ms budget | `sacn.cpp` |
| RDM TX→RX turnaround | ~3 ms | No network preemption on core 1 | `rdm_engine.h:19` |
| RDM response timeout | 9 ms | UART RX window | `rdm_engine.h:19` |
| RDM discovery | 8 s budget (total) | DISC_UNIQUE_BRANCH search | `rdm_disc.cpp` |
| ArtSync commit grace | 1000 ms | `ARTEGNSYNC_TIMEOUT_MS` | `artnet.h:74` |
| sACN Stream Sync grace | 500 ms | Sync loss timeout | `sacn.h` |

## 15. Traceability

| Claim | Source |
|---|---|
| 5-layer model: drv → cfg → core → net → app/sys | `platformio.ini:2` |
| `main.cpp` is 166 lines (thin wiring layer) | `main.cpp:1-166` |
| Core pinning: core 0 = net/serial/websocket, core 1 = DMX/RDM TX | `tasks.cpp:78-85` |
| AsyncTCP pinned to core 0 to avoid preempting RDM | `platformio.ini:46-50` |
| RDM TX→RX inflation measured 15→53 µs median | `platformio.ini:42-45` |
| SeqLock: 32-bit ticket, odd=mid-write, 8 retries | `seqlock.h:11-31` |
| ArtRdmResp ring: core 1 → core 0, volatile + `__sync_synchronize` | `art_rdm_resp_queue.h:5-8` |
| Config resolution: neutral → template → NVS | `config_core.cpp:38-39,157-211` |
| RDM: RMT-TX + RX-only UART (never released mid-frame) | `rdm_engine.h:2-5` |
| DMX uses RMT, not UART (issue #64) | `dmx_rmt.h:1-9` |
| RMT byte LUT: precompute, memcpy per slot | `dmx_rmt.h:43-79` |
| Output init crash guard: NVS counter, progressive disable | `tasks.cpp:39-76` |
| 1 ms DMX tick, 2 ms net tick | `tasks.cpp:138-144,146-155` |

## 16. Cross-References

- [Index](./INDEX.md)
- [Include Headers](./include-headers.md)
- [Config Engine](./config-engine.md)
- [DMX Buffer](./core-dmx-buffer.md)
- [Merge Engine](./core-merge-engine.md)
- [Frame Router](./core-frame-router.md)
- [RDM Engine](./core-rdm-engine.md)
- [RDM Task](./core-rdm-task.md)
- [Art-Net Protocol](./net-artnet-protocol.md)
- [sACN Protocol](./net-sacn-protocol.md)
- [WebSocket Protocol](./net-websocket-protocol.md)
- [OTA](./net-ota.md)
- [Ethernet](./net-ethernet.md)
- [Tasks](./sys-tasks.md)
- [Crash Guard](./sys-crash-guard.md)
- [Build System](./build-system.md)
- [Test Infrastructure](./test-infrastructure.md)

## 17. Limitations

- `sanitizeOutputs()` (called at `main.cpp:47`) is not defined in any inspected source file — its implementation is not determinable from the inspected source code.
- The WiFi disconnection/reconnection handler after initial connect is not visible — only `startWiFiStation` in `net_state.cpp` is implemented. Not determinable from the inspected source code — whether a `WiFiEvent` handler exists elsewhere.
- sACN discovery (E1.31 Universe Discovery, vector 0x04) — the constant exists but the implementation is not visible.

## 18. Open Questions

- Not determinable from the inspected source code — the implementation of `sanitizeOutputs()` (called `main.cpp:47`).
- Not determinable from the inspected source code — whether WiFi roaming/disconnect events are handled after initial connection.
- Not determinable from the inspected source code — the full sACN `readSacnSocket` implementation body.
- Not determinable from the inspected source code — the exact Stream Sync 500 ms timeout value (declared in header, enforced in sacn.cpp body not fully inspected).

## 19. Testing

| Test | File | Coverage |
|---|---|---|
| Config engine smoke test | `test/native/config_test.cpp` | Template resolution, setValue/getValue, NVS round-trip, serial console |
| Seqlock test | `test/native/seqlock_test.cpp` | Snapshot during write, stable write, 100 cycles |
| Merge engine test | `test/native/merge_test.cpp` | HTP, OFF/LTP, LOSS_ZERO, LTP-Takeover, priority merge |
| RDM types test | `test/native/rdm_types_test.cpp` | UID pack/unpack, enums, PID constants |
| Unity config test | `test/unit-test/test_config/test_unit_config.cpp` | Defaults, set/get, NVS roundtrip, template, serial commands |

See [Test Infrastructure](./test-infrastructure.md) for the full test framework and execution model.

## 20. History

| Date | Change | Source |
|---|---|---|
| 2026-08-15 | Core pinning documented: AsyncTCP → core 0 | `platformio.ini:42-50` |
| 2026-08-15 | RMT replaces UART for DMX TX (issue #64) | `dmx_rmt.h:1-9` |
| 2026-08-15 | seqlock replaces mutex for DMX buffer | `seqlock.h:5` |
| 2026-08-15 | RDM moved to separate core-1 task | `rdm_task.h:2-3` |
