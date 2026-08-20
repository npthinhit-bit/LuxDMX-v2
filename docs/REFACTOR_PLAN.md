# LuxDMX-v2 Refactoring Plan — ESP-IDF Migration (docs-derived)

**Status:** Plan v1 — derived solely from `docs/` (47 spec files under `docs/SYSTEM_SPECIFICATION/`, `docs/websocket-protocol.md`, `codebase_index.md`, `Lessons_Learned.md`).
**Target:** Art-Net/sACN → DMX512 gateway firmware on pure ESP-IDF v5.2, built with PlatformIO.
**Target boards (all must work):** ESP32-S3-WROOM-2 N16R8 (`esp32s3_psram`), WT32-ETH01 (`wt32eth01`), ESP32-DevKit (`esp32dev`).

---

## 1. Documentation Sufficiency Assessment

### 1.1 Bottom line

> **The `docs/` tree is sufficient as a *black-box behavioral specification* to rebuild the system from scratch with zero coupling to the legacy codebase** — *for everything that ships*. It pins down nearly every concrete value an implementer needs (ports, task table, field schemas, wire formats, timing, state machines, error tables, test inventories). It is **not** sufficient as an implementation-fidelity contract for (a) the handful of subsystems the docs themselves admit are stubbed/unimplemented, (b) a consolidated per-board pin table, and (c) the ESP-IDF *bindings* for APIs the docs describe with Arduino idioms (see §1.3, §1.4).

Key data points that make the docs high-value (all cross-checked against the spec sheaf):

| Concern | Pinned by spec | Where |
|---|---|---|
| Layer model, core-affinity strategy | Strict core 0 (network/web) vs core 1 (DMX/RDM prio 18–19) isolation | 01, 34, 46 |
| Task table | `dmxTx` C1 prio 19, 1 ms, 8 KB; `netRx` C0 prio 5, 2 ms, 8 KB; `led` prio 1, 50 ms, 2 KB; `display` 200 ms, 4 KB; `versionCheck` 60 s, 12 KB | 34 |
| Seqlock DMX buffer contract | 1032 B/output (live 513 + ArtSync 513+3 + sACN 513+3 + sync 2); 8-retry snapshot; 1 ms tick; ~4.1 KB DRAM, zero heap/PSRAM | 03, 45 |
| RMT DMX512 timing | 1 MHz clock, 4 µs bit, break 176 µs, MAB 12 µs, 3000 symbol cap, start code 0x00/0xCC | 14, 42 |
| Protocol sockets | Art-Net UDP 6454, sACN 5568, WS 80, HTTP 80, mDNS `dmx-gateway.local`, syslog 514 | 01 |
| Config schema | NVS namespace `"dmxgw"`; key schemes `a_*`/`b_*`/`c_*`/`d_*`; legacy `o0_*`/`o1_*`→`a_*`/`b_*` and `apfb`→`fbmode` one-shot migration; 47 global + 24/output fields; flag semantics (LIVE/REBOOT/SECRET/NOWEB/KEEPNE) | 02, 45 |
| WS binary frame | Exact 2095-byte layout, big-endian, deltas, changed-universe bitmap, RDM tail, 12 client slots, sequence skip | 23, websocket-protocol.md |
| REST surface | Full route table (data endpoints, config, setup portal, reset/reboot, OTA, RDM, LED) | websocket-protocol.md, 25, 26 |
| Setup portal | Activation: no SSID **or** GPIO0 LOW 3 s **or** connect-fail-no-AutoIP; open AP (SSID = hostname), wildcard DNS :53, 302 captive redirect, `POST /setup` (ssid, psk) → NVS → reboot | 33 |
| OTA + Ed25519 | Streaming task, boot-retry `dmxgw/boottry` cap 3, sig = final 64 B R‖S over image-64, `OTA_SIGN_ENABLED` gate | 29, 30 |
| Test inventory | Native suites (config 5, seqlock 3, merge 8, rdm_types 10) + Unity (config 8, …) + Playwright E2E; host shim table | 46, 47 |

### 1.2 Coverage matrix

| Deliverable (Phase-agnostic) | Spec coverage | Verdict |
|---|---|---|
| Config field schema / NVS keys | 02, 45 | ✅ Fully specified (names, ranges, flags, migration). See §1.3-g for two live/reboot + importJson caveats. |
| Web REST + routes + JSON contract | 25, 26, websocket-protocol.md | ✅ Fully specified. `log.json` (`[]`) and `/labels` (`{}`) are documented *stubs*. |
| WebSocket message schema | 23, websocket-protocol.md | ✅ Byte-exact. Gap: no WS command-result relay (24 §OQ). |
| Board pin/peripheral tables | scattered (16 DE/RE, 36 LED, 37 display) | ⚠️ **Gap.** No consolidated per-board pin table in any spec or template. |
| Task list / priorities / core affinity | 34, 46 | ✅ Fully specified. |
| DMX512 timing | 14, 03, 34 | ✅ Fully specified. |
| RDM (engine/discovery/task/transport) | 09, 10, 11, 15, 16 | ✅ Fully specified; **TodRequest is an explicit no-op** in 19. |
| OTA + Ed25519 | 29, 30 | ✅ Fully specified; `otaPassword` unused + `autoUpdate` has no edge (29 §OQ). |
| Ethernet (SPI W5500 / RMII LAN8720) | 31, 32, 45 | ✅ Behaviorally specified; PHY driver *bodies* not specified (Arduino singleton cited). |
| Test infra / HIL | 46, 47, 34 | ⚠️ Specs reference native **runner scripts that do not exist** on disk (46/47 §OQ). |
| LED status | 36 | ⚠️ `off`/plain-GPIO fully specified; **WS2812 + 5-LED panel render paths are historical stubs** (36 §OQ3). |
| Crash guard | 35, 34 | ✅ Fully specified (TTL 3000 ms, disable range, NVS `dmxgw/dmxcrash`). |
| Build system (codegen, hook) | 46 | ⚠️ Describes a Python template/PROGMEM **generator that does not exist** in the current tree; current `platformio.ini` has no hooks. |

### 1.3 Identified gaps and remediation

| # | Gap | Evidence | Remedy |
|---|---|---|---|
| a | **No consolidated per-board pin/peripheral mapping.** | Pins are scattered (16 DE/RE, 36 LED, 37 display) and no board lists all of them; `templates/*.ini` only covers 3 of the boards and its keys predate the live C schema. | Produce the canonical `BOARD` pin table in one header (deliverable of this plan, §5.3) and make the config board-template layer read from it. Keep `templates/*.ini` as the generator input format (46) once the generator exists. |
| b | **Arduino-native API bindings in specs must be re-expressed as ESP-IDF.** | `PROGMEM`/gzip asset gen (27, 46), `WiFiUDP` (18), `AsyncWebServer`+`AsyncTCP` (25, 26, 28, 29, 34), `HTTPClient`/`Update` (29), `Preferences` (02, 47), `DNSServer` (33), `ESP.getFreeHeap()` (websocket-protocol.md). | The docs pin *behavior*; the plan re-implements each against ESP-IDF v5.2: `esp_http_server` (or `httpd` on lwIP), `esp_netif`/`esp_wifi`, lwIP `dns_setserver`/custom DNS task, `esp_ota_ops`, `nvs_handle_t`, `esp_app_desc`. Notably the **wildcard captive DNS** has no stock ESP-IDF component — plan custom lwIP DNS server (raw UDP task, per 33 semantics). AsyncTCP equivalent = nettask pinned to core 0 (34). |
| c | **Config schema delta: docs say 47 global + 24/output; current C tree has ~13** global fields and no output struct. | 45 vs `components/lux_config/src/config_schema.c`. | Phased schema growth toward the documented surface (Phase 2). Treat 45 as the field-count contract. |
| d | **Stubbed subsystems the docs admit.** | display `renderDisplay` stub (37); WS2812/5-LED panel LED paths (36 §OQ3); `log.json`/`/labels` stubs (26); RDM `TodRequest` no-op (19 §OQ); `loopback` field unconsumed (06 §OQ); WS command-result relay (24 §OQ); `otaPassword` no check, `autoUpdate` no edge (29 §OQ); `set_LED_brightness`/`boot_connecting_LED` no caller traced (36 §OQ1-2). | Decision policy: *adopt documented behavior; where the spec itself says stub, either implement or formally cut, then update the spec + `Lessons_Learned.md` in the same commit as the decision. Track via a "parity register" (§10).* |
| e | **Build/test tooling assumed by specs missing on disk.** | Native runner scripts (46/47 §OQ); template + PROGMEM generators (46); partition CSV not referenced by `platformio.ini`. | Phase 0 implements the tooling as PlatformIO `extra_scripts` / `pre_cmds`, keeping 46's behavior (sort templates base-first, embed as constant strings, gzip HTML assets into headers). |
| f | **Open questions unresolved in the docs themselves.** | net/subnet `CFG_LIVE` vs reboot (06 §OQ2); dynamic Retry-After, hits metric, weighted tokens (28 §OQ); importJson recursion into `outputs` array (02 §OQ3); `CFG_KEEPNE` honored at load or only web route (02 §OQ1); OTA cancellation (29 §OQ); `dmxRateHz` consumer + `syncMode` producer (34 §OQ). | Each becomes a decision record in `Lessons_Learned.md` or a spec amendment at the phase that implements it (see phase sections). No phase may ship with an unresolved OQ that gates its exit criteria. |
| g | **Config apply-semantics ambiguities** (subset of f). | 06 net/subnet flagged. | Resolve at Phase 2: prefer reboot semantics for net/subnet (avoid transient misrouting) unless the merge/frame-router phase proves otherwise. |

### 1.4 Verdict per phase

- **Sufficient now:** Phase 1 (network baseline — every behavior traceable to 33/36/01/02/43), Phase 2 (config engine — 02/45), Phase 3 (DMX TX — 03/14/42/45), Phase 4 (protocols + merge — 04/05/06/17–22), Phase 5 (RDM — 09/10/11/15/16/19-with-parity-note).
- **Needs remediation first:** Phase 6 (web — needs WS-result relay decision, 24 OQ, and bindings b), Phase 7 (system services — several stubs d, Ethernet PHY body unspecified), Phase 8 (release — needs runner scripts e).

---

## 2. Plan Ground-Rules

- **Parity definition:** "existing features" = the behavioral surface pinned by the 47 specs + `websocket-protocol.md`. New work must match those constants exactly (ports, timings, keys, byte offsets, task table). Anything undocumented is *not* a parity obligation.
- **Docs evolve with code:** updating a spec, `codebase_index.md`, or `Lessons_Learned.md` is required content in the same commit as the behavior it describes.
- **One logical feature per commit**, each green (unit+lint+build). No WIP commits.
- **UI-first:** the web frontend is a standalone project (`webui/` dir, plain HTML/CSS/JS) developed and tested in a browser against a host `native` shim before any device integration (per 47 third tier).
- **Test orders:** host-native (per 47 `check`-macro suites) → Unity-on-device (`esp32_dev_c` style) → Hardware-in-the-Loop (HIL) where a bench exists. Every behavior change adds ≥1 test to an existing suite.
- **Build DNA is preserved:** PlatformIO stays the build system (46); device-test and partition layout are PlatformIO-managed, not hand-edited (`sdkconfig*`, root `CMakeLists.txt`, `dependencies.lock`, `managed_components/` are generated).

---

## 3. Target ESP-IDF Component Structure & Kconfig Layout

### 3.1 Component layout (mirrors the 5-layer spec model)

```
main/                          # thin wiring (app_main), no logic
  main.cpp  main/*.c           # boot sequence per 01 setup phases
components/
  lux_common/                  # spec 45 include-headers layer (no logic)
    include/ lux_types.h  cfg.h  seqlock.h  dmx_err.h  rdm_types.h  eth_phy.h  constants.h
    src/    constants.c
  lux_config/                  # cfg layer, spec 02
    config_schema.c  config_engine.c  config_serial.c  config_json.c  config_xml.c  template_gen.c
  lux_drv/                     # drv layer, specs 14/15/16/36/37
    dmx_rmt_tx.c  dmx_uart_rx.c  gpio_dir.c  led_status.c  display.c
  lux_core/                    # core layer, specs 03–13, 42
    dmx_buffer.c  output_init.c  merge_engine.c  sender_tracker.c  frame_router.c
    scene_engine.c  rdm_engine.c  rdm_discovery.c  rdm_task.c  stats.c
    input_router.c  enc_decode.c
  lux_net/                     # net layer, specs 17–33
    artnet.c  sacn.c  art_packet_queue.c  art_rdm_resp_queue.c  sacn_packet_queue.c
    artnet_bridge.c  websocket.c  ws_handler.c  ws_frame.c  rate_limiter.c
    web_server.c  web_routes.c  web_frontend.c  web_assets.c
    ethernet.c  net_state.c  setup_portal.c  dns_server.c
    ota.c  ota_sign.c
  lux_sys/                     # sys layer, specs 34–41
    tasks.c  crash_guard.c  firmware_version.c  syslog.c  alert.c  soak_monitor.c
  lux_test/                    # spec 47 — host shims + Unity stubs only, never built into firmware
    host/ (nvs_shim, string_shim, heap_shim, rmt_shim, net_sim)
    unity/ (test natives pulled from component src via selective compile)
webui/                         # standalone browser frontend (Phase 6; provisioning subset in Phase 1)
  index.html  css/ js/  src/   # mock.ts consuming the REST/WS contract via a native shim
```

**Ownership rules:** `lux_net` owns the `"dmxgw"` NVS handled namespace and socket/HTTP lifecycle; `lux_core` owns the seqlock-protected DMX buffers (read by `lux_drv` RMT TX only through the snapshot API); nothing outside `lux_sys` creates tasks; `lux_common` is a header-only contract layer (no runtime functions except inline seqlock primitive and enum mappers per 45).

### 3.2 Kconfig layout (multi-board)

ESP-IDF Kconfig is a validation/option layer layered **under** the PlatformIO per-env definitions (46 keeps per-env build flags as source of truth).

```
Kconfig                     # project root
  menu "LuxDMX Gateway"
    config LUXDMX_MAX_OUTPUTS     int, range 1..4   # HW guard: ESP32-S3 has 4 RMT TX (45 §10) — Kconfig dependency, not just pragma
    config LUXDMX_MAX_SENDERS     int (16)
    config LUXDMX_RDM_TOD_MAX     int (64)
    config LUXDMX_LOG_BUF_CAP     int (32)
    config LUXDMX_DEFAULT_TEMPLATE      # choice: per board, injected as -DLUXDMX_DEFAULT_TEMPLATE="name"
    config LUXDMX_OTA_SIGN_ENABLED bool  # gated to release envs (esp32s3_n16r8_eth style), see 30/46
    config LUXDMX_SOAK_TEST       bool  # only esp32s3_psram/4uni soak envs, see 38
components/lux_*/Kconfig          # per-component options: PHY family choice, WS max clients, RDM timeouts
sdkconfig.defaults{.esp32s3_psram, .wt32eth01, .esp32dev}   # per-env IDF overrides, PlatformIO-generated (46 §7): brownout-disable on S3 (boot-loop fix), esp-modbus exclusion (Kconfig conflict), NO="-I src" include-isolation for S3.
```

Board selection today = `[env:*] board_build.mcu` + per-env `build_flags` (46's `-D` approach). Kconfig adds **validation dependencies** (e.g., `LUXDMX_MAX_OUTPUTS` must be 1 when the S3 target cannot claim >4 RMT channels; PHY choice conflict warnings). Document the mapping in `codebase_index.md` Build section.

### 3.3 Partition layout

Keep the tracked dual-slot OTA `partitions.csv` (nvs 0x5000, otadata 0x2000, two 0x1E0000 OTA apps, spiffs 0x20000, coredump 0x10000) and reference it explicitly from `platformio.ini` (`board_build.partitions = partitions.csv`) — the current file does not, so the layout is effectively dead (46 §OQ2 remediation).

---

## 4. Phased Roadmap (with exit criteria)

Phase gating rule: a phase exits only when every exit criterion is green **on all 3 target boards** (hardware or reliable Wokwi simulation), and `codebase_index.md` + `Lessons_Learned.md` are updated.

### Phase 0 — Scaffold & tooling (no device behavior)
- PlatformIO envs aligned to 3 boards; Kconfig skeleton; partitions wiring; generated-file hygiene; clang-format config; host-native harness + shim skeletons (47 shim table); CI skeleton (build + native test only).
- **Exit:** `pio run -e {esp32dev,wt32eth01,esp32s3_psram}` all succeed (ESP-IDF v5.2), native harness runs an empty suite green, one commit per tool.

### Phase 1 — Network baseline (this plan's MVP; detail in §5)
- Status LED (all 3 boards), WiFi station w/ configurable creds, SoftAP fallback + captive-provisioning portal, NVS persistence of creds, structured logging, serial console (provisioning subset).
- **Exit:** see §5.5.

### Phase 2 — Config engine (full schema)
- Complete 47-global / 24-per-output schema (45), board templates via generator (46), NVS overall + `migrateNvsKeys` (`o0_*`/`o1_*`→`a_*`/`b_*`, `apfb`→`fbmode`), full serial grammar (`dump`/`get`/`set`/`save [reboot]`/`wifi`/`factory`/`help` — 02, 43), JSON export/import + XML import/export, `CFG_*` flags honored.
- **Exit:** `config_test` (5) + Unity `test_unit_config` (8) green; template inheritance ≤8 levels; secret masking verified over serial + web; migration idempotency test added (02 §12 gap).

### Phase 3 — DMX output core (drv + core TX path)
- `dmx_output_t` runtime structs + `resolveOutputMode`/`sanitizeOutputs`/`outputInitAll` (07, 45), per-output DMX buffers behind seqlock (03), RMT TX engine (14) with break/mark 176/12 µs, DMX transmit task C1 prio 19 1ms tick (34), start-code handling (42), stage/flush ArtSync+sACN 513+3 staging with 1000/500 ms timeouts (03).
- **Exit:** seqlock native test (3) + merge-output wiring test; RMT waveform measured on scope meets 14 table; monitor mode (test pattern) observable over serial; `dmxBufWriteEndSet`/staging OQs (03 §OQ3-4) resolved in-code.

### Phase 4 — Network protocols & routing (net + core)
- Art-Net (17) + sACN (18) sockets on C0 `netRxTask` prio 5, frame router (06), merge engine (04 HTP/LTP/Priority + loss modes), sender tracker (05, 2500 ms timeout), packet queues (20/21/22) with bounds (8/2 ms, per-socket 4), bridge (19) with **TodRequest parity decision**.
- **Exit:** loopback DMX over two gateways or Wokwi packet-injection; merge 8-test suite green; `rxFrameCount`/`outSrcLost` counters visible in stats (13); no heap in the RX hot path (640 B static bufs).

### Phase 5 — RDM
- RDM types (45), engine + discovery + task (09/10/11), UART RX + DE/RE GPIO (15/16) incl. 90 µs settle and 8N2 250 kbaud, cross-core resp ring (21), setaddr/identify/setpers/setlabel/bqp over REST+WS (26), `rdm_task` C1 prio 18.
- **Exit:** rdm_types native suite (10) green; full discovery on 1 fixture on bench; turnaround meets 34 HIL median (~≤53 µs after AsyncTCP-core-0 pin); TODO: confirm `TodRequest` behavior per §1.3-d.

### Phase 6 — Web application (full UI) + WebSocket
- Standalone `webui/` app; REST surface per websocket-protocol.md; WS 2095-byte binary frame + meta + commands (23, websocket-protocol.md); delta/subscription semantics; rate limiter (28); command-result relay decision (24 §OQ).
- **Exit:** Playwright E2E green against native shim (mock transport); on device: ws frame byte-conformance checked by fixture test; rate limiter `error` severity enforced (429 + Retry-After); log/labels parity per §1.3-d.

### Phase 7 — System services & scaling
- Tasks layer complete (34), crash guard (35), LED panel + WS2812 render paths (36 gap-d), display (37, replace stub with real or formally cut), OTA + Ed25519 (29/30), firmware-version check (39), syslog (40), alert webhooks (41), soak monitor (38), Ethernet SPI/RMII + fallback policy (31/32; PHY bodies per availability), rate-limiter API exposure (28 §OQ5).
- **Exit:** OTA rollback-on-bad-signature HIL; crash-guard recovery loop trialed; 4-universe `esp32s3_n16r8_eth`-style env if added; soak 60 s passes.

### Phase 8 — Hardening & release
- Full 6-env matrix (46), Wokwi smoke for all, soak/HIL regression sweep, ESP-MQTT/include-isolation + libatomic edge verification (46 §OQ3), release packaging + downstream upgrade guide.
- **Exit:** full parity register complete; all docs OQ-bearing sections either resolved or annotated with decision.

---

## 5. Phase 1 — Network Baseline (detailed)

### 5.1 Scope → spec traceability

| Requirement (brief) | Spec anchor |
|---|---|
| Status LED, board-specific mapping | 36 (types 0–2 for Phase 1; panel type 3 deferred to Phase 7) |
| WiFi station, configurable credentials | 01 (STA bring-up), 32 (net-state), 02 (wifi_ssid/wifi_psk CFG_LIVE per 36-table? *mark: WiFi SSID is CFG_REBOOT per 45*) |
| SoftAP fallback + captive portal provisioning | 33 (activation conditions, DNS, POST /setup) |
| Persistent credential storage | 02 NVS `"dmxgw"` namespace; 45 `CFG_SECRET` masking |
| Structured logging (serial now, web later) | 40 (RFC 5424 style), 43 (serial verbs), 13/23 (32-entry log ring) |
| Serial console (provisioning subset) | 43, 02 §2 |

### 5.2 Board pin/peripheral mapping table (Phase 1 surface)

| Peripheral | ESP32-S3-WROOM-2 N16R8 (`esp32s3_psram`) | WT32-ETH01 (`wt32eth01`) | ESP32-DevKit (`esp32dev`) | Via |
|---|---|---|---|---|
| Status LED | **GPIO 48, WS2812** (`ledType=2`) per spec 46 env table ("WS2812 LED on GPIO48") + `templates/esp32s3_psram.ini` | **Gap** — no LED pin documented for this board in any spec/template. *Assumption:* onboard indicator; confirm on HW in the first HIL commit and pin in the board table (remediation a). | **GPIO 2**, plain GPIO (default `ledtype=1`, `ledpin=2` in `_base.ini` legacy syntax) | `ledPin`/`ledType` (36), PWM LEDC 1 kHz/8-bit (36 §8) or RMT+WS2812 for type 2 |
| Forced-setup/BOOT | GPIO0 LOW ≥ 3 s (33 §2/§8) | GPIO0 (same activation; the WT32-ETH01 has an on-board BOOT) | GPIO0 | `BOOT` input |
| Console UART | UART0, 115200 8-N-1 (43) | UART0 | UART0 | `monitor_speed=115200` |
| WiFi antenna/PHY enable | N/A (built-in RF) | RMII LAN8720 built-in (45/46: NetIF_RMII); RF unused | N/A | 31 (Phase 7 applies Ethernet) |
| AP/DNS for portal | SoC radio, lwIP DNS :53 (33) | same | same | custom `dns_server.c` (idf binding) |
| RMT channels (reserve) | 4 max (45 §10) | 4 | 4 | Phase 3 |
| DMX TX/RX/RTS (Phase 3+) | board template values (02) — pins finalized at Phase 3 | same | same | 14/15/16 |

All pins are config-overridable at runtime via the schema; this table sets the *board default template*. Adding the table to `lux_common/boards.c` (name, `board_type_t`, led, wifi, eth, console) satisfies gap-a and mirrors `hw.h`'s `board_config_t`.

### 5.3 Task breakdown (in dependency order)

1. **T0 Tooling/CI:** host-native harness builds; `pio run` matrix green (Phase 0 continues into Phase 1 first commit).
2. **T1 Board layer:** `boards.c` table (S3/WT32/DevKit), `hw_init()` board detection, per-env `DEFAULT_TEMPLATE` routing. *Test:* native config template-resolution for each board.
3. **T2 Logger:** structured logger (levels, timestamps, 32-entry ring) → serial (43), later `log.json`/meta-push. *Test:* native ring wrap + level filtering.
4. **T3 Config core (Phase-1 subset):** schema table for `hostname`, `wifi_ssid`, `wifi_psk` (SECRET|KEEPNE), `ap_ssid`/`ap_password`, `led_pin`/`led_type`, `autoip`; neutral→template→NVS resolution; `save`/`load`, clamp, secret masking. *Test:* `config_test` subset (set/get, round-trip, clamp, mask).
5. **T4 LED status:** `led_status` per 36 for types 0/1/2 (PWM + WS2812), 400 ms boot blink, network-green `0x000a00`, brightness clamp. *Test:* pure luminance/brightness mapping unit; HIL blink observation.
6. **T5 WiFi STA:** `net_state` + station connect from config, event callbacks, backoff; SSID-empty/password handling. *Test:* native with `net_sim` shim; HIL connect to test AP.
7. **T6 Setup portal:** SoftAP (SSID=hostname, open), wildcard DNS (lwIP-based), portal flag, main-loop DNS pump, 302 redirect, `GET /setup` page, `POST /setup` (ssid, psk) → NVS → reboot. *Test:* portal state machine native test (activation matrix of 33 §2); HIL: force via GPIO0 hold, provision, observe reboot into STA.
8. **T7 Serial console (subset):** verbs `help`, `dump`, `set k=v`, `get k`, `save [reboot]`, `wifi <ssid> [pass]`, `reboot` (02 §2/43). *Test:* grammar tests in `config_test`.
9. **T8 Provisioning frontend (standalone):** `webui/` app — scan list, AP page, save; run against native REST/WS shim in browser (47 tier 3). No device needed.
10. **T9 Integration + HIL commit:** boot→(no creds)→portal→provision→reboot→STA→LED green; edge: bad psk retry; disconnect→reconnect.

### 5.4 Test strategy (Phase 1)

- **Native host (`check` macro, 47):** new `net_baseline_test` + `config_test` subset + `led_math_test`. Host shims: `nvs_shim` (in-memory map, `Preferences::clearAll`-equivalent), `string_shim`, `net_sim`. Full suite in CI on every commit.
- **On-device Unity:** `esp32_dev_c`-style `pio test` for config LED/WiFi-sensitive paths (subset).
- **HIL (bench):** (a) scope/visual LED blink on all 3 boards; (b) connect to provisioned AP → confirm STA IP via DHCP/mDNS; (c) GPIO0 3 s hold → portal forced even when creds exist; (d) reboot persistence of creds across 2 resets; (e) captive detection on phone/laptop (manual, gated N/A in CI).
- **Browser E2E (47 tier 3):** Playwright drives `webui/` against native shim — scan, submit, persist, error paths. Never needs hardware.

### 5.5 Exit criteria (Phase 1)

1. All three boards: configure creds via portal → device reboots into STA and reports IP over serial (`net_local_ip` per 32) and LED green; creds survive 2+ reboots (NVS).
2. Forced portal via GPIO0 works even with valid creds; `POST /setup` refuses nothing that 33 allows; portal DNS wildcard resolves any name to AP IP.
3. Logger: levels respected, ≥32 entries retained, serial grammar per 43 passes native grammar tests.
4. `pio run -e {esp32dev,wt32eth01,esp32s3_psram}` green; native suite (config/led/net-state/portal) green in CI; Wokwi smoke boots S3 (LED blink) in CI.
5. `board_templates[]`/boards table documents the WT32-ETH01 LED resolution (gap-a closed); `codebase_index.md` phase-1 entry and `Lessons_Learned.md` Phase-1 section added.

### 5.6 Commit sequence (one logical feature per commit)

1. `build: wire partitions.csv + Kconfig skeleton + three envs consistent`
2. `common: add boards.c board table + hw_init board detection`
3. `log: structured logger (levels, ring, serial sink)`
4. `config: phase-1 schema (wifi/hostname/led/autoip) + NVS overlay + masking`
5. `config: serial console subset (help/dump/get/set/save/wifi/reboot)`
6. `led: led_status types 0/1/2 + boot blink + brightness unit test`
7. `net: station connect from config + net_state + backoff + event cb`
8. `net: setup portal (AP + wildcard DNS + flag + DNS pump)`
9. `web: minimal httpd (esp_http_server): /, /setup, /setup/scan, 302, /info.json, /health, /reboot`
10. `webui: standalone provisioning app against native shim` (frontend commits may be split: html → css → js/mock)
11. `test: native net_baseline_test + portal state machine matrix`
12. `test: Unity on-device config/led subset`
13. `ci: HIL gating job + Wokwi S3 smoke job`
14. `docs: update specs touched (33/36), codebase_index.md, Lessons_Learned.md (Phase 1)`

Each commit green: `pio run -e native -t exec` (native tests) + targeted build before push.

### 5.7 Frontend integration contract (Phase 1 subset) — anchored to websocket-protocol.md

REST (running on ESP-IDF `esp_http_server`, port 80):

| Method | Path | Request | Response | Notes |
|---|---|---|---|---|
| GET | `/setup` | – | HTML provisioning page (AP mode) | portal active only (33) |
| GET | `/setup/scan` | – | `[{"ssid":"…","rssi":-45,"open":true,"secure":true},…]` (26 route; DTO pins in the web frontend spec 27) | portals use scan to prefill |
| POST | `/setup` | form `ssid`, `psk` | `302` → `/` then reboot after persist (33) | persists to NVS `"dmxgw"`, secret-masked |
| GET | `/info.json` | – | `{"name":hostname,"ver":"0.0.0-dev","sdk":…,"heap":…,"rssi":…,"ip":"…"}` (26) | heartbeat/first paint |
| GET | `/health` | – | `{"status":"ok","outputs":[…]}` (26) | CI poll target |
| POST | `/reboot` | – | `{"ok":true}` | after portal save or factory flows |

Gateway/not-found: while portal flag set, unregistered paths → 302 to `/` (33 §2). All JSON responses `Cache-Control: no-store` (websocket-protocol.md REST section).

WebSocket (`ws://host/ws`): Phase 1 uses the **text meta subset only** — the full 2095-byte binary frame (23/websocket-protocol.md) is introduced in Phase 6. Phase-1 event schema (JSON text frames, posted ~2 Hz, mirror of meta frame):

```
{"meta":1,
 "state":"portal|connecting|station|ap-only",
 "net":{"ip":"…","rssi":-45,"ssid":"…"},
 "led":{"color":"0x000a00","on":true},
 "log":["…"]}            # 32-entry ring tail (13 §-)
```

Browser→device commands for Phase 1: none beyond REST; `subscribe`/`set`/`blackout`/`scene`/`rdm` commands are Phase 6. The `webui/` app must isolate transport so a `MockTransport` (fetch/WS stubbed) drives all screens in the browser with zero hardware — this mock is also the Playwright harness source.

### 5.8 Risks carried into Phase 1

- WiFi SSID flagged `CFG_REBOOT` (45) — changing via `POST /setup` then rebooting satisfies 33's reboot requirement; device **must** tolerate a brief IL-/unreachable window.
- Captive DNS has no ESP-IDF component (binding b) — mitigation: lwIP raw-UDP task, size bounded, no heap; if infeasible, gate on Wokwi validation.
- WT32-ETH01 has no documented LED (gap-a) — mitigation: resolve at first HIL commit; until then board template falls back to `ledType=0` (off, degraded-but-safe per 36 §7).

---

## 6. CI/CD Pipeline Sketch (GitHub Actions; maps to GitLab with runner labels)

```yaml
# .github/workflows/ci.yml  — every PR + main
on: [pull_request, push]            # branches: idf-only, main

jobs:
  native-unit:                      # host GCC, no hardware, spec 47
    runs-on: ubuntu-latest
    steps: pio install → pio run -e native -t exec   # config_test, seqlock_test, merge_test, rdm_types_test, net_baseline
  static-analysis:
    runs-on: ubuntu-latest
    steps: cppcheck (src-filters per component, unusedFunction), clang-tidy (compile_commands macro set), clang-format diff check
  firmware-build:                   # matrix of all env targets, spec 46
    strategy: matrix env=[esp32dev, wt32eth01, esp32s3_psram]   # expand to 6 in Phase 8
    steps: pio run -e $env → upload artifacts (firmware.bin, partitions.bin)
  wokwi-smoke:                      # on-device simulation for esp32s3_psram (led boot blink, portal activate)
    steps: build → wokwi-ci run (timeout gated) → parse serial for LOG markers (33/36 exit cues)
  webui-e2e:                        # browser tier, spec 47 tier 3
    steps: node webui → playwright against native shim (mock transport) → coverage report
  hil-gate:                         # manual/dispatch-only (protected env on main); runs on hardware bench
    steps: flash → assert STA connect → provision → reboot → LED green → NVS persistence ×2
  release:                          # tag vX.Y.Z
    steps: build 6-env matrix → Ed25519 sign (gitignored keys, tools/gen_ota_keys.py recovery) → GitHub release artifacts
```

HIL is never a blocking CI check (no bench in upstream); it is a manual workflow with device fixtures (33/34 HIL measurements). Static analysis is a gate from Phase 2 on. Artifacts cache `~/.platformio` to avoid megabyte re-downloads.

---

## 7. Living-Docs Discipline (from day one)

- Every merged commit touching behavior MUST update, as applicable: the affected spec (`docs/SYSTEM_SPECIFICATION/NN-*.md`), `codebase_index.md` (component/status tables + architecture decisions), `Lessons_Learned.md` (following the existing Phase/pitfall/lesson format). CI adds a docs-diff check so a code-merge without doc-update fails the PR.
- Maintain the **parity register** (§10) — a table of spec items vs implementation phase vs status (`spec'd / in-progress / done / cut-with-reason`).

---

## 8. Phase 2–8 Summary (deliverable-level)

| Phase | Key deliverables | Spec anchors |
|---|---|---|
| 2 | Full schema (47+24×4), template generator (46), JSON/XML import-export, NVS migration, serial grammar full | 02, 43, 45, 46 |
| 3 | Seqlock DMX buffer, RMT TX engine, output runtime init, 1 ms TX task | 03, 07, 14, 34, 42 |
| 4 | Art-Net/sACN protocol + queues, frame router, merge engine, sender tracker, stats | 04, 05, 06, 13, 17–22 |
| 5 | RDM engine/discovery/task, UART RX, DE/RE GPIO, cross-core resp ring | 09, 10, 11, 15, 16, 19 |
| 6 | Standalone web UI, full REST + WS binary protocol, rate limiter, command relay decision | 23–28 + websocket-protocol.md |
| 7 | Tasks layer, crash guard, LED panel/WS2812, display, OTA+sign, syslog, alert, soak, Ethernet | 29–41 |
| 8 | 6-env matrix, HIL soak sweep, libatomic/ESP-MQTT edge, release workflow | 46, 47, 31 |

---

## 9. Phase 2–8 Commit/Test Gating

- **Config (P2):** native `config_test` 5 + Unity 8; new migration + template-inheritance tests (closing 02 §12 gaps).
- **DMX core (P3):** seqlock suite 3; RMT timing scope assertion; `dmxBufWriteEndSet`/staging OQ resolution in-code.
- **Protocols (P4):** merge suite 8; loopback packet-injection; parse-limit tests (8 pkts/2 ms, 640 B caps).
- **RDM (P5):** rdm_types 10; discovery bench; RTDD/turnaround HIL measurement ≤ documented median.
- **Web (P6):** Playwright against native shim = primary; on-device byte-conformance fixture; rate-limiter tests added (28 has none today).
- **System services (P7):** OTA rollback HIL; crash-guard 3-s cycle trial; soak 60 s; LED panel/WS2812 render tests (closing 36 §OQ3).
- **Hardening (P8):** full matrix build in CI; Wokwi smoke per env; release signing pass.

---

## 10. Parity Register (seed — grows with each phase)

| Spec item | Status | Where | Decision/Notes |
|---|---|---|---|
| mDNS `dmx-gateway.local` (01) | spec'd | Phase 1/7 | lwIP mdns component |
| Wildcard captive DNS (33) | spec'd/blocked | Phase 1 | no ESP-IDF component → custom lwIP task (binding b) |
| WS 2095-byte binary frame (23) | spec'd | Phase 6 | Phase 1 ships JSON meta subset only |
| RDM `TodRequest` (19) | spec'd/no-op | Phase 5 | parity decision: implement-or-cut, record in Lessons_Learned |
| LED WS2812 + 5-LED panel paths (36) | spec'd/stub | P1(WS2812)/P7(panel) | S3 needs WS2812 → implemented in Phase 1; panel at Phase 7 |
| Display `renderDisplay` (37) | spec'd/stub | Phase 7 | implement-or-cut decision required |
| `/labels` persistence (26) | spec'd/stub | Phase 7 | NVS vs localStorage decision (26 §OQ) |
| `loopback` field (06) | spec'd/unconsumed | Phase 4 | implement-or-remove |
| `otaPassword` / `autoUpdate` (29) | spec'd/gap | Phase 7 | implement or formally drop + spec note |
| net/subnet live semantics (06) | OQ | Phase 2/4 | prefer reboot; validate with loopback |
| Native runner script (46/47) | done | Phase 0 | tools/native_run.py delivered (commit 22307f8) |
| Template/PROGMEM generators (46) | in-progress | Phase 2 | template text embedded as constant strings in config_schema.c; build hook generator (46) deferred to Phase 8 |
| Config schema delta (1.3-c, 45) | done | Phase 2 | 47 global + 24/output fields implemented with offsetof descriptors (commit 4966809) |
| NVS key migration (02 §6.1, 45) | done | Phase 2 | migrateNvsKeys() implemented, idempotency test green |
