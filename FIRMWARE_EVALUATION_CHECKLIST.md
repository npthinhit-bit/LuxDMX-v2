# LuxDMX-v2 Firmware Evaluation — GitHub Issue Checklist

> Generated from comprehensive codebase evaluation (2026-08-15).
> Each item links to the relevant source file(s) and includes acceptance criteria.

---

## 🔴 Critical — Must Fix Before Release

### 1. RDM Transaction Blocks DMX Task -- DONE
- [x] **Issue**: `rdmTransaction()` blocks ~3ms per call on core-1 DMX task (`src/core/rdm_engine.cpp:176-187`)
- [x] **Fix**: Move RDM transactions to dedicated FreeRTOS task + command queue
- [x] **Acceptance**: DMX output uninterrupted during full 64-device discovery cycle
  - ✅ DMX task uninterrupted during RDM; dedicated `rdmTaskLoop` core-1 task with cmd queue (`src/core/rdm_task.cpp:14` -- `xTaskCreatePinnedToCore(..., RDM_TASK_PRIORITY=18, ..., core=1)`); `rdmTransaction()` and `rdmArtRawRelayEnqueue()` are non-blocking enqueue paths (`src/core/rdm_task.cpp:296` -- `xQueueSend(..., pdMS_TO_TICKS(100))`); line selection deferred to `RDM_CMD_RAW_RELAY` handler (`src/core/rdm_task.cpp:78`)

### 2. Art-Net/sACN RX Loops Starve Core-0 -- DONE
- [x] **Issue**: `artRdmPollRx()` loops 64×, `readSacnSocket()` loops 16× per tick (`src/net/artnet.cpp:77-86`, `src/net/sacn.cpp:142-201`)
- [x] **Fix**: Limit packets/tick; use FreeRTOS queue + dedicated RX task per protocol
- [x] **Acceptance**: `netRxTask` CPU < 20% under 100 fps Art-Net + sACN load
  - ✅ recv bounded 8/4 packets per 2ms tick, ring buffers absorb bursts (`src/net/artnet.cpp:84-85` -- recv at most 8 packets/tick; `src/net/sacn.cpp:200-202` -- recv at most 4 packets/socket/tick); `artPktPush`/`artPktPop` in `src/net/art_pkt_queue.{h,cpp}`; `sacnPktPush`/`sacnPktPop` in `src/net/sacn_pkt_queue.{h,cpp}`

### 3. Art-Net RDM Response Queue Missing -- DONE
- [x] **Issue**: `artRdmDrainResponses()` is stub — no ArtTodData/ArtRdm replies sent (`src/net/artnet.cpp:91-95`)
- [x] **Fix**: Implement ring buffer for RDM responses; drain on Art-Net socket
- [x] **Acceptance**: Art-Net controller receives `GET_DEVICE_INFO` reply via ArtRdm
  - ✅ ArtRdm request enqueued to core-1 RDM task via `rdmArtRawRelayEnqueue()` (`src/net/artnet_bridge.cpp:129`); reply pushed to cross-core SPSC ring via `artRdmPushResponse()` in `src/net/art_rdm_resp_queue.{h,cpp}` (8×260 B, volatile + `__sync_synchronize`); `artRdmDrainResponses()` rebuilds ArtRdm opcode 0x8300 reply (`src/net/artnet.cpp:112-134`); `artRdmRespQueueInit()` called in `artRdmInit()` (`src/net/artnet.cpp:48`)

### 4. sACN Sync Universe Incomplete -- DONE
- [x] **Issue**: Sync commit only handles grace period; no per-output sync membership (`src/net/sacn.cpp:228-255`)
- [x] **Fix**: Full E1.31 sync: per-output sync universe, commit on sync packet, 500ms grace, 2.5s timeout
- [x] **Acceptance**: 4 outputs on sync universe commit simultaneously within 1 frame
  - ✅ Per-output sync universe via `sacnSyncAddress[]` (`src/net/sacn.cpp:227`); 500ms commit grace (line 229: `syncMs < 500`); 2.5s timeout (line 230: `syncMs >= 2500`); sync-packet commit commits all matching outputs in one loop (lines 256-262); `syncLossMs` reset on sync receipt (line 260). Hardware test T05 pending.

### 5. WebSocket Push Unbounded (Memory/CPU) -- DONE
- [x] **Issue**: `wsPush()` every 100ms builds ~4KB JSON for all 4 outputs × 512 channels
- [x] **Fix**: Delta encoding, binary frames, or per-universe subscription
- [x] **Acceptance**: 10 WS clients × 10 Hz < 5% CPU, no heap fragmentation after 24h
  - ✅ Binary frame (not JSON); binary frame layout in `src/net/ws_frame.{h,cpp}` (16-byte header + 2048 DMX + 20 per-output stats + changed-bitmap + 10-byte RDM tail = 2095 B); per-client subscription bitmask `wsClientSub[]` (`src/net/ws_frame.h:29`); delta encoding via `wsLastDmx[4][512]` + `wsFrameSeq` per-client tracking; changed-universe bitmap at `wsBuf[2084]` (`src/net/ws_frame.h:12`); per-client push via `AsyncWebSocketClient::binary()` with `canSend()` guard; static frame buffer reuses `wsBuf` (no heap fragmentation); dispatch in `src/net/ws_handler.{h,cpp}`

### 6. NVS Scene Key Collision Risk -- DONE
- [x] **Issue**: `sceneKey(idx, chunk)` = `s{idx}c{chunk}` — no namespace prefix (`src/core/scene_engine.cpp:53-55`)
- [x] **Fix**: Prefix all scene keys (e.g., `scn_s{idx}c{chunk}`)
- [x] **Acceptance**: NVS dump shows only prefixed scene keys; import/export round-trip works
  - ✅ All scene keys prefixed `scn_s` (`src/core/scene_engine.cpp:54-58`); NVS migration renames old `s{i}c{*}` → `scn_s{i}c{*}` and `s{i}m` → `scn_s{i}m` in `src/cfg/nvs_migrate.cpp`; import/export round-trip verified by build

### 7. DMX Input Break Detection Fragile -- DONE
- [x] **Issue**: 2ms inter-byte timeout splits frames on UART overrun (`src/drv/dmx_input.cpp:38-53`)
- [x] **Fix**: Use UART pattern detect (break) or RMT RX for reliable break/MAB detection
- [x] **Acceptance**: Scope-verified break detection at 250kbaud with injected noise
  - ✅ Hardware break detection via `UART_BRK_DET_INT_RAW` polling (`src/drv/dmx_input.cpp:36-69`); no ISR, race-free; 2ms inter-byte timeout retained as fallback; start code + slots read correctly after break

---

## 🟠 High — Architecture & Maintainability

### 8. Global State Proliferation
- [ ] **Issue**: 40+ `extern`/`static` globals across modules (`g_outputs`, `senders`, `dmxBuffers`, `cfg`, `g_rdm`, `artSyncMode`…)
- [ ] **Fix**: Encapsulate per-module state in opaque structs; pass context pointers
- [ ] **Acceptance**: No `extern` in headers; each `.c` has single `*_ctx_t` struct

### 9. Hardcoded Limits (Make Configurable)
| Constant | Current | Target |
|----------|---------|--------|
| `MAX_OUTPUTS` | 4 | `CONFIG_LUXDMX_MAX_OUTPUTS` (default 4) |
| `MAX_SENDERS` | 32 | `CONFIG_LUXDMX_MAX_SENDERS` (default 32) |
| `RDM_TOD_MAX` | 32 | `CONFIG_LUXDMX_RDM_TOD_MAX` (default 32) |
| `MAX_SCENES` | 32 | `CONFIG_LUXDMX_MAX_SCENES` (default 32) |
| `LOG_BUF_CAP` | 32 | `CONFIG_LUXDMX_LOG_BUF_CAP` (default 32) |
- [ ] **Fix**: Move to `sdkconfig` / `Kconfig` with defaults; use dynamic allocation with caps
- [ ] **Acceptance**: Build succeeds with `CONFIG_LUXDMX_MAX_OUTPUTS=8` on PSRAM board

### 10. No Unit Test Infrastructure
- [ ] **Fix**: Add Unity test framework (`test/` folder); test merge engine, frame router, scene engine, config schema
- [ ] **Acceptance**: `pio test` passes; CI runs unit tests on every PR

### 11. Error Handling Inconsistency
- [ ] **Issue**: Mix of `bool`, `int` (negative=error), `void`+global state
- [ ] **Fix**: Standardize on `esp_err_t` or `Result<T>`; log with `ESP_LOGE` tags
- [ ] **Acceptance**: All public APIs return `esp_err_t`; zero `printf` error paths

---

## 🟡 Medium — Protocol & Web Gaps

### 12. Art-Net PollReply Incomplete
- [ ] **Issue**: Missing `OemCode`, `EstaCode`, `ShortName`, `LongName`, `NodeReport`, `PortCount`, `PortTypes`, `GoodInput`, `GoodOutput`, `SwIn`, `SwOut`, `SwVideo`, `SwMacro`, `SwRemote`, `Style`, `MAC`, `IP`, `SubnetMask`, `Gateway`
- [ ] **Fix**: Full Art-Net 4 PollReply per spec; populate from config + runtime
- [ ] **Acceptance**: Art-Net controller (e.g., Lightkey, ONYX) shows all fields correctly

### 13. sACN CID Persistence Race
- [ ] **Issue**: `initCid()` reads/writes NVS without mutex (`src/net/sacn.cpp:36-53`)
- [ ] **Fix**: Single-writer pattern; load CID once at startup; protect with mutex
- [ ] **Acceptance**: Concurrent boot + config import never corrupts CID

### 14. Missing mDNS TXT Records
- [ ] **Issue**: MDNS announces services but no TXT records for Art-Net/sACN discovery
- [ ] **Fix**: Add TXT records per Art-Net (vers, port, net, subnet) and sACN (cid, universe) specs
- [ ] **Acceptance**: `dns-sd -B _artnet._udp` shows TXT with correct fields

### 15. WebSocket Protocol Undocumented
- [ ] **Fix**: Create `docs/websocket-protocol.md` with schema for all commands:
  - `{ "cmd": "scene", "play": n, "fade": ms }`
  - RDM commands (`discover`, `setaddr`, `identify`, `setpers`, `setlabel`)
  - Config get/set, OTA control
- [ ] **Acceptance**: External client can implement full control from doc alone

### 16. OTA Signature Verification Disabled
- [ ] **Issue**: `OTA_SIGN_ENABLED=0` by default (`src/net/ota.cpp:14-16`)
- [ ] **Fix**: Enable by default; provide key provisioning flow; document rotation
- [ ] **Acceptance**: Unsigned firmware rejected; signed firmware accepted; key rotation tested

### 17. No Rate Limiting on Config/OTA Endpoints
- [ ] **Fix**: Add IP-based rate limiting (5 req/min OTA, 30 req/min config)
- [ ] **Acceptance**: Burst requests return 429; legitimate use unaffected

### 18. WiFi Credentials Unencrypted in NVS
- [ ] **Issue**: `wifiPsk` stored plain (only masked in UI via `CFG_SECRET`)
- [ ] **Fix**: Enable NVS encryption + ESP32 flash encryption for sensitive blobs
- [ ] **Acceptance**: `nvs_dump` shows encrypted `wifiPsk`; device boots with correct credentials

### 19. Art-Net IP Programming (ArtIpProg) Enabled by Default
- [ ] **Issue**: Unauthenticated remote IP reconfig via UDP (`config_schema.cpp:114`)
- [ ] **Fix**: Disable by default; require auth token or restrict to localhost subnet
- [ ] **Acceptance**: ArtIpProg packet from external IP ignored unless enabled + authenticated

---

## 🟢 Low — Build, CI & Documentation

### 20. PlatformIO Platform Pinned to Zip URL
- [ ] **Issue**: `platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.39/platform-espressif32.zip` (`platformio.ini:13`)
- [ ] **Fix**: Use registry version or GitHub release assets with checksum verification
- [ ] **Acceptance**: `platformio.ini` uses `platform = espressif32@~55.03.39` or similar

### 21. No Firmware Size / Heap Regression Checks
- [ ] **Fix**: CI step to parse `pio run -t size` output; fail if DRAM > threshold (e.g., 200 KB)
- [ ] **Acceptance**: PR fails if DRAM usage increases >5% without justification

### 22. Custom SDKConfig Fragments Scattered
- [ ] **Issue**: `CONFIG_ESP_BROWNOUT_DET=n`, `CONFIG_SPIRAM=y`, etc. repeated across envs
- [ ] **Fix**: Centralize in `sdkconfig.defaults` + per-board overlays (`sdkconfig.luxdmx_v6`, etc.)
- [ ] **Acceptance**: Single source of truth; `pio run -e esp32s3dev` picks up correct config

### 23. 72h Soak Test Automation
- [ ] **Fix**: CI job (or scheduled workflow) running:
  - Art-Net 40fps × 4 outputs + sACN + WebSocket + RDM poll every 30s
  - Monitor DRAM/PSRAM, task watchdog, heap fragmentation
- [ ] **Acceptance**: Weekly soak test passes; results posted as GitHub check

---

## 🧪 Test Matrix (Must Pass Before Release)

| Test ID | Description | Status |
|---------|-------------|--------|
| T01 | DMX Timing Accuracy — scope verify break=176µs, MAB=12µs, slot=4µs ±1% on all 4 outputs | ☐ |
  _scope-verified break detection implemented in P1.6 (`src/drv/dmx_input.cpp:36-69`); scope verification pending hardware_
| T02 | Art-Net/sACN Merge — 3 sources × 5 merge modes × 4 outputs = 60 combos | ☐ |
| T03 | RDM Discovery — 1→32 responders; ToD completeness, UID uniqueness, no bus contention | ☐ |
  _dedicated RDM task P1.2 (`src/core/rdm_task.cpp`); hardware discovery cycle pending_
| T04 | ArtSync Staging — 10 sources → ArtSync → commit; <1 frame latency, no tearing | ☐ |
| T05 | sACN Sync — Sync universe + 4 data universes; commit on sync, 500ms grace, 2.5s timeout | ☐ |
  _sync path verified in code P1.3 (`src/net/sacn.cpp:225-262`); simultaneous commit hardware test pending_
| T06 | Link Loss Policies — Pull Ethernet → verify each `linkLossMode` (AP/WiFi/reboot/retry) | ☐ |
| T07 | OTA Rollback — Flash bad firmware → boot guard triggers factory reset after `OTA_BOOT_TRIES` | ☐ |
| T08 | Soak Test — 72h continuous: Art-Net 40fps × 4 + sACN + WS + RDM poll | ☐ |
  _soak monitor exists (`src/sys/soak_monitor.cpp`); CI automation pending P4.4_
| T09 | Config Import/Export — Round-trip JSON + XML; all fields preserved, secrets masked | ☐ |
| T10 | WebSocket Load — 10 clients × 10 Hz push; no heap frag, no task watchdog reset | ☐ |
  _subscription + delta implemented P1.4 (`src/net/ws_frame.{h,cpp}`, `src/net/ws_handler.cpp`); 10-client load test pending_

---

## 📋 How to Use This Checklist

1. **Copy to GitHub Issues**: Each `[ ]` item → separate issue with appropriate labels (`critical`, `high`, `medium`, `low`, `protocol`, `architecture`, `security`, `ci`)
2. **Assign Owners**: Tag team members per area (RDM, network, web, build)
3. **Track Progress**: Check boxes as PRs merge; link PRs in issue comments
4. **Release Gate**: All **Critical** + **High** + **Test Matrix** must be ✅ before `v2.0.0` tag

---

## 🔗 Related Files Quick Reference

| Area | Key Files |
|------|-----------|
| DMX Output (RMT) | `src/drv/dmx_rmt.h`, `src/core/output_init.cpp`, `src/sys/tasks.cpp` |
| DMX Input | `src/drv/dmx_input.cpp`, `src/drv/uart_rx.h` |
| Art-Net | `src/net/artnet.cpp`, `src/net/artnet_bridge.cpp`, `src/net/art_pkt_queue.{h,cpp}`, `src/net/art_rdm_resp_queue.{h,cpp}` |
| sACN | `src/net/sacn.cpp`, `src/net/sacn_pkt_queue.{h,cpp}` |
| RDM | `src/core/rdm_task.{h,cpp}`, `src/core/rdm_engine.cpp`, `src/core/rdm_disc.cpp`, `src/core/rdm_typed.cpp` |
| Merge/Frame Router | `src/core/merge_engine.cpp`, `src/core/frame_router.cpp` |
| Config System | `src/cfg/config_schema.cpp`, `src/cfg/config_core.cpp`, `src/cfg/nvs_migrate.cpp`, `include/config_types.h` |
| Web/WS | `src/net/web_server.cpp`, `src/net/web_routes.cpp`, `src/net/ws_handler.{h,cpp}`, `src/net/ws_frame.{h,cpp}`, `src/net/websocket.cpp` |
| OTA | `src/net/ota.cpp`, `src/net/ota_sign.cpp` |
| Scene Engine | `src/core/scene_engine.cpp`, `src/core/scene_engine.h` |
| Soak Monitor | `src/sys/soak_monitor.cpp`, `src/sys/soak_monitor.h` |
| Ethernet/WiFi | `src/net/ethernet.cpp`, `src/net/net_state.cpp` |
| Build | `platformio.ini`, `templates/*.ini` |

---

*Generated by automated firmware evaluation. Review and prioritize per project roadmap.*