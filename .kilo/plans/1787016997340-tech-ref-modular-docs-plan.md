# Technical Reference Documentation — Modular Docs Plan

## Goal

Produce a modular technical reference in `docs/TECHNICAL_REFERENCE/` that documents the LuxDMX-v2 firmware architecture for onboarding and contributor reference. Each module file covers one functional domain, with line-by-line source traceability and cross-references.

## 21 Structural Principles & Analysis Rules

Every module file MUST follow these 21 principles in order:

1. **Title & Identity** — H1 heading: `<Domain> — Technical Reference`. Sub-heading: `Domain: <layer>.<subsystem>`.
2. **Domain Scope** — One `## 1. Domain Scope` section: what this module owns, what it delegates, what consumes it.
3. **Architecture Layer Mapping** — Section `## 2. Layer Mapping` — maps the domain to the 5-layer model (`drv → cfg → core → net → app/sys`).
4. **Source Tree Layout** — Section `## 3. Source Files` — table of all files in scope with role per file (`src/... → Class/method()`).
5. **Data Structures** — Section `## 4. Data Structures` — every struct/enum/class used by this domain, field-by-field, with file:line references.
6. **Concurrency Model** — Section `## 5. Concurrency` — core pinning, priority, lock primitives, SPSC ring involvement, seqlock usage. If single-threaded, state "Single-threaded (core 0 only)".
7. **State Machine** — Section `## 6. State Machine` — explicit states, transitions, entry/exit events. If not applicable, state "No state machine — stateless request/response."
8. **Entry Points** — Section `## 7. Entry Points` — functions the scheduler/dispatcher calls first; exact call site per entry point.
9. **Data Flow** — Section `## 8. Data Flow` — numbered step-by-step flow with file:line references (producer → queue → consumer).
10. **Protocol/Packet Layout** — Section `## 9. Protocol Layout` — if this module speaks a wire protocol, table of fields/byte offsets/sizes with references. If not, "N/A (no wire protocol)."
11. **Configuration Integration** — Section `## 10. Config Integration` — which `Config` fields (config_schema.cpp) this domain reads/writes, CFG_LIVE vs CFG_REBOOT. "None" if no config dependency.
12. **Lifecycle** — Section `## 11. Lifecycle` — init sequence, periodic task hooks, shutdown/cleanup. Exact function names per phase.
13. **Error Handling** — Section `## 12. Error Handling` — return values (`esp_err_t`/`bool`/`int`), error logging (`ESP_LOGE`/`Serial.printf`), fallback behavior.
14. **Memory Allocation** — Section `## 13. Allocation` — static buffers, PSRAM heap_caps, stack sizes per task, `MALLOC_CAP_*` flags.
15. **Timing Constraints** — Section `## 14. Timing` — deadlines in µs/ms, measured values, tolerance. "Best-effort (no hard deadline)" if none.
16. **Traceability / Evidence** — Section `## 15. Traceability` — REQUIRED table: claim → exact file:line reference. Every behavioral statement must cite a line.
17. **Cross-References** — Section `## 16. Cross-References` — links to other module files (`[Name](./file.md)`) + the specific functions they consume/deny. "No cross-references" if standalone.
18. **Known Limitations** — Section `## 17. Limitations` — hard truths from source. If nothing, state "No known limitations from inspected source."
19. **Open Questions** — Section `## 18. Open Questions` — items not determinable from code. Each: "Not determinable from the inspected source code — `<what needs checking>`."
20. **Validation & Tests** — Section `## 19. Testing` — unit test file references (`test/unit-test/test_*.cpp`), native test references (`test/native/*_test.cpp`), hardware test references. "No test coverage for this domain." if absent.
21. **Change History** — Section `## 20. History` — brief log of decisions that changed this domain (from FIRMWARE_EVALUATION_CHECKLIST.md, feature-audit-plan.md, git). "No recorded changes." if none.

---

## Output Directory

```
docs/TECHNICAL_REFERENCE/
├── INDEX.md
├── architecture-overview.md
├── build-system.md
├── config-engine.md
├── drv-dmx-rmt-tx.md
├── drv-dmx-uart-rx.md
├── drv-gpio-dir.md
├── core-dmx-buffer.md
├── core-merge-engine.md
├── core-sender-tracker.md
├── core-frame-router.md
├── core-scene-engine.md
├── core-output-init.md
├── core-rdm-engine.md
├── core-rdm-discovery.md
├── core-rdm-task.md
├── core-rdm-typed.md
├── core-input-router.md
├── core-stats.md
├── net-artnet-protocol.md
├── net-sacn-protocol.md
├── net-artnet-bridge.md
├── net-art-pkt-queue.md
├── net-art-rdm-resp-queue.md
├── net-sacn-pkt-queue.md
├── net-websocket-protocol.md
├── net-websocket-handler.md
├── net-web-server.md
├── net-web-routes.md
├── net-web-frontend.md
├── net-rate-limiter.md
├── net-ota.md
├── net-ota-sign.md
├── net-ethernet.md
├── net-net-state.md
├── net-setup-portal.md
├── sys-tasks.md
├── sys-crash-guard.md
├── sys-led-status.md
├── sys-soak-monitor.md
├── sys-firmware-version.md
├── sys-syslog.md
├── sys-alert.md
├── app-enc-decode.md
├── app-input-map.md
├── app-menu.md
├── include-headers.md
└── test-infrastructure.md
```

## Existing Documentation to Cross-Reference

The following files already exist in `docs/` and MUST be referenced (not duplicated) by module docs:

| File | Covers |
|---|---|
| `docs/websocket-protocol.md` (436 lines) | Full WS binary frame layout, text commands, REST API |
| `docs/ota-key-management.md` (181 lines) | Ed25519 key generation, embedding, signing, rotation |

- `net-websocket-protocol.md` should reference `docs/websocket-protocol.md` for detailed frame layout + REST API, and focus on the C++ producer/dispatch side
- `net-ota-sign.md` + `net-ota.md` should reference `docs/ota-key-management.md` for key management procedures

## Module-to-Source File Mapping

| Doc Module | Source Files |
|---|---|
| `config-engine` | `src/cfg/config_core.cpp`, `config_core.h`, `config_schema.cpp`, `config_serial.cpp`, `config_serial.h`, `nvs_migrate.cpp`, `nvs_migrate.h`, `src/config_templates_gen.cpp` |
| `drv-dmx-rmt-tx` | `src/drv/dmx_rmt.h` |
| `drv-dmx-uart-rx` | `src/drv/dmx_input.cpp`, `dmx_input.h`, `uart_rx.h` |
| `drv-gpio-dir` | `src/drv/gpio_dir.h` |
| `core-dmx-buffer` | `src/core/dmx_buffer.cpp`, `dmx_buffer.h`, `include/seqlock.h` |
| `core-merge-engine` | `src/core/merge_engine.cpp`, `merge_engine.h` |
| `core-sender-tracker` | `src/core/sender_tracker.cpp`, `sender_tracker.h` |
| `core-frame-router` | `src/core/frame_router.cpp`, `frame_router.h` |
| `core-scene-engine` | `src/core/scene_engine.cpp`, `scene_engine.h` |
| `core-output-init` | `src/core/output_init.cpp`, `output_init.h`, `include/output.h` |
| `core-rdm-engine` | `src/core/rdm_engine.cpp`, `rdm_engine.h`, `include/rdm_types.h` |
| `core-rdm-discovery` | `src/core/rdm_disc.cpp`, `rdm_disc.h` |
| `core-rdm-task` | `src/core/rdm_task.cpp`, `rdm_task.h` |
| `core-rdm-typed` | `src/core/rdm_typed.cpp` |
| `core-input-router` | `src/core/input_router.cpp`, `input_router.h` |
| `core-stats` | `src/core/stats.cpp`, `stats.h` |
| `net-artnet-protocol` | `src/net/artnet.cpp`, `artnet.h` |
| `net-sacn-protocol` | `src/net/sacn.cpp`, `sacn.h` |
| `net-artnet-bridge` | `src/net/artnet_bridge.cpp` |
| `net-art-pkt-queue` | `src/net/art_pkt_queue.cpp`, `art_pkt_queue.h` |
| `net-art-rdm-resp-queue` | `src/net/art_rdm_resp_queue.cpp`, `art_rdm_resp_queue.h` |
| `net-sacn-pkt-queue` | `src/net/sacn_pkt_queue.cpp`, `sacn_pkt_queue.h` |
| `net-websocket-protocol` | `src/net/ws_frame.cpp`, `ws_frame.h`, `websocket.cpp`, `websocket.h` |
| `net-websocket-handler` | `src/net/ws_handler.cpp`, `ws_handler.h` |
| `net-web-server` | `src/net/web_server.cpp`, `web_server.h` |
| `net-web-routes` | `src/net/web_routes.cpp`, `web_routes.h` |
| `net-web-frontend` | `src/frontend/web_frontend.cpp`, `web_frontend.h`, `src/frontend/base/*.h`, `src/frontend/pages/*.h`, `src/frontend/scripts/*.h` |
| `net-rate-limiter` | `src/net/rate_limiter.cpp`, `rate_limiter.h` |
| `net-ota` | `src/net/ota.cpp`, `ota.h` |
| `net-ota-sign` | `src/net/ota_sign.cpp`, `ota_sign.h`, `tools/sign_ota.py` |
| `net-ethernet` | `src/net/ethernet.cpp`, `ethernet.h`, `include/eth_phy.h` |
| `net-net-state` | `src/net/net_state.cpp`, `net_state.h` |
| `net-setup-portal` | `src/net/setup_portal.cpp` |
| `sys-tasks` | `src/sys/tasks.cpp`, `tasks.h` |
| `sys-crash-guard` | `src/core/output_init.cpp` (dmxInitGuard*), `include/output.h` |
| `sys-led-status` | `src/sys/led_status.cpp`, `led_status.h` |
| `sys-soak-monitor` | `src/sys/soak_monitor.cpp`, `soak_monitor.h` |
| `sys-firmware-version` | `src/sys/firmware_version.cpp`, `firmware_version.h` |
| `sys-syslog` | `src/sys/syslog.cpp`, `syslog.h` |
| `sys-alert` | `src/sys/alert.cpp`, `alert.h` |
| `app-enc-decode` | `src/app/enc_decode.h` |
| `app-input-map` | `src/app/input_map.h` |
| `app-menu` | `src/app/menu.h` |
| `include-headers` | `include/config_schema.h`, `config_types.h`, `config_enums.h`, `output.h`, `seqlock.h`, `rdm_types.h`, `eth_phy.h` |
| `test-infrastructure` | `test/native/*_test.cpp`, `test/unit-test/test_*.cpp`, `test/native/shim/*`, `test/native/unity_host.c` |

## INDEX.md — Master Index

### 1. System Overview (embedded)
- 5-layer model: drv → cfg → core → net → app/sys
- Core pinning: core 0 = net/serial/websocket, core 1 = DMX/RDM TX
- Concurrency primitives: FreeRTOS, seqlock, SPSC rings, semaphores

### 2. Architecture Layer Map
| Layer | Dir | Module Links |
|---|---|---|
| drv | src/drv/ | [DMX RMT TX](./drv-dmx-rmt-tx.md), [DMX UART RX](./drv-dmx-uart-rx.md), [GPIO DIR](./drv-gpio-dir.md) |
| cfg | src/cfg/ | [Config Engine](./config-engine.md) |
| core | src/core/ | [DMX Buffer](./core-dmx-buffer.md), [Merge Engine](./core-merge-engine.md), [Sender Tracker](./core-sender-tracker.md), [Frame Router](./core-frame-router.md), [Scene Engine](./core-scene-engine.md), [Output Init](./core-output-init.md), [RDM Engine](./core-rdm-engine.md), [RDM Discovery](./core-rdm-discovery.md), [RDM Task](./core-rdm-task.md), [RDM Typed](./core-rdm-typed.md), [Input Router](./core-input-router.md), [Stats](./core-stats.md) |
| net | src/net/ | [Art-Net Protocol](./net-artnet-protocol.md), [sACN Protocol](./net-sacn-protocol.md), [ArtNet Bridge](./net-artnet-bridge.md), [Art Pkt Queue](./net-art-pkt-queue.md), [Art RDM Resp Queue](./net-art-rdm-resp-queue.md), [sACN Pkt Queue](./net-sacn-pkt-queue.md), [WebSocket Protocol](./net-websocket-protocol.md), [WebSocket Handler](./net-websocket-handler.md), [Web Server](./net-web-server.md), [Web Routes](./net-web-routes.md), [Web Frontend](./net-web-frontend.md), [Rate Limiter](./net-rate-limiter.md), [OTA](./net-ota.md), [OTA Sign](./net-ota-sign.md), [Ethernet](./net-ethernet.md), [Net State](./net-net-state.md) |
| sys | src/sys/ | [Tasks](./sys-tasks.md), [Crash Guard](./sys-crash-guard.md), [LED Status](./sys-led-status.md), [Soak Monitor](./sys-soak-monitor.md), [Firmware Version](./sys-firmware-version.md), [Syslog](./sys-syslog.md) |
| app | src/app/ | [Enc Decode](./app-enc-decode.md), [Input Map](./app-input-map.md), [Menu](./app-menu.md) |

---

## Execution Order (for implementation agent)

1. Write `INDEX.md` (master index + system overview table)
2. Write `architecture-overview.md` + `build-system.md` (foundational)
3. Write `include-headers.md` + `config-engine.md` (schema/types shared by all)
4. Write `drv-*` files (foundational drivers)
5. Write `core-dmx-buffer.md` → `core-merge-engine.md` → `core-sender-tracker.md` → `core-frame-router.md` (DMX pipeline, data dependency order)
6. Write `core-output-init.md` (bridges drivers → core)
7. Write `core-rdm-engine.md` → `core-rdm-discovery.md` → `core-rdm-task.md` → `core-rdm-typed.md` (RDM stack top-down)
8. Write `core-input-router.md` + `core-scene-engine.md` + `core-stats.md` (remaining core)
9. Write `net-*` files (protocols → bridges → queues → web → OTA → ethernet → setup-portal)
10. Write `sys-*` files (tasks → crash guard → led → soak → version → syslog → alert)
11. Write `app-*` files (enc → input-map → menu)
12. Write `test-infrastructure.md` (last — references all tested modules)
13. Run `pio run -e esp32s3_psram` to verify no doc-only build breakage

## Constraints

- NO source code changes — documentation only
- Every behavioral claim cites `file → line_number` from actual inspected source
- "Not determinable from the inspected source code" for any unverifiable claim
- Relative links between module files only (no absolute paths)
- Build gate: `pio run -e esp32s3_psram` must remain green (documents don't affect build)

## Validation

- Each module file contains all 20 required sections (headers 1–20) per the 21 principles
- Traceability table non-empty in every module
- Cross-references resolve to existing files in the tree
- No speculative claims without "Not determinable" qualifier
