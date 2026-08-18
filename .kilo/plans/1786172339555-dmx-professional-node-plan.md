# LuxDMX-v2 Professional DMX Node — Implementation Plan

## Goal
Implement the full "Firmware — DMX Art-Net/sACN Node (4 Universe)" specification on the existing V2 modular codebase (`drv → cfg → core → net → app/sys` architecture, ESP32-S3 + W5500 target). The spec describes 4 universes of DMX with full Art-Net 4 + sACN support, extensive merge/failover capabilities, presets, timecode, management/monitoring, and OTA config.

## Current State Analysis

The V2 codebase (`main.cpp` ~130 lines + 5-layer architecture) already implements a substantial core:

### Already Implemented
| Spec Feature | Status | Location |
|---|---|---|
| Art-Net 4 (ArtPoll/PollReply, ArtDmx, ArtSync, ArtAddress, ArtIpProg, ArtNzs) | DONE | `src/net/artnet.cpp`, `artnet_bridge.cpp` |
| Art-Net RDM gateway (ArtRdm, ArtTod*) | DONE | `src/net/artnet_bridge.cpp:111`, `src/core/rdm_engine.cpp` |
| sACN unicast/multicast/broadcast receive | DONE | `src/net/sacn.cpp` |
| Source priority per universe (0-200) | DONE | `sender_tracker.cpp` (priority field, `merge_engine.cpp` top-prio filter) |
| Art-Net + sACN simultaneous | DONE | `cfg.protocol` enum, `main.cpp` starts both |
| sACN Universe Discovery | PARTIAL | `sacn.cpp:107-108` (consume/discard only, no transmit) |
| sACN Stream Sync | DONE | `sacn.cpp` staged buffers with 500ms commit grace |
| 4 universes, 512 channels each | DONE | `config_schema.h:6`, `MAX_OUTPUTS=4` |
| Merge up to 8 sources per universe | DONE | `sender_tracker.h:6` `MAX_SENDERS=16` (exceeds spec requirement) |
| HTP / LTP / LTP-Takeover | DONE (HTP/LTP), **MISSING** (LTP-Takeover) | `merge_engine.cpp` has `MERGE_OFF/HTP/LTP` only |
| Priority-based merge mode | PARTIAL | Priority filtering exists within HTP/LTP; no standalone "priority-only" mode |
| Per-port failsafe (Hold/Go to Preset/Go Dark/Go to Home) | DONE (Hold/Zero/Stop) | `LOSS_HOLD=0, LOSS_ZERO=1, LOSS_STOP=2` — **missing: Preset/Home** |
| Failsafe timeout per port (0-600s) | **MISSING** | Global `SOURCE_TIMEOUT_MS=2500` only |
| DMX refresh rate tunable (1-44Hz) | DONE | `tasks.cpp:DMX_RATE_MS` {25,24,30,40,50ms} = 40/41.7/33.3/25/20Hz |
| Start code passthrough | DONE | `ArtNzs` in `artnet.cpp`, `routeFrameNzs` |
| RDM full discovery + get/set | DONE | `rdm_disc.cpp`, `rdm_typed.cpp`, 19 PIDs |
| Invert DMX polarity | **MISSING** | No polarity field |
| Break/MAB time configurable | **MISSING** | Hard-coded `RMT_DMX_BREAK=176`, `RMT_DMX_MAB=12` in `dmx_rmt.h:21-22` |
| Port isolation (input ≠ output) | DONE | RMT TX-only on output pins; no DMX input path exists |
| DHCP / Static IP | DONE | `net_state.cpp`, `config_schema.cpp` |
| AutoIP (169.254.x.x) | **MISSING** | Not implemented |
| WiFi STA/AP, mesh-aware strongest AP | DONE | `net_state.cpp` |
| mDNS/Bonjour | DONE | `main.cpp:75` `MDNS.begin` |
| QoS/DSCP marking | **MISSING** | No DSCP config |
| VLAN tagging (802.1Q) | **MISSING** | No VLAN support |
| SNMP trap | **MISSING** | No SNMP |
| NTP time sync | **MISSING** | No NTP/time sync |
| Web UI (responsive, no app) | DONE | `src/pages/*.html`, `extra_scripts.py` |
| REST API (get/set config, status, preset) | DONE | `web_routes.cpp` |
| WebSocket push (live DMX, stats) | DONE | `websocket.cpp`, `ws_frame.cpp` |
| OLED/LCD display + encoder | PARTIAL | Stubs in `led_status.cpp`, `display.cpp` |
| Per-port DMX live monitor | DONE | `handleDmxJson` returns all 512 channels per output |
| Per-port packet loss, FPS, source IP | DONE | `/senders.json`, `ws_frame.cpp` |
| Syslog (local + remote) | **MISSING** | No syslog module |
| Email/webhook alerts | **MISSING** | No alert module |
| OTA firmware update (HTTP/TFTP) | DONE | HTTP OTA in `ota.cpp`; **TFTP missing** |
| Signed firmware | DONE | `ota_sign.cpp` (Ed25519 framework, `OTA_SIGN_ENABLED`) |
| Dual firmware bank (rollback) | DONE | `ota.cpp` boot counter (`OTA_BOOT_TRIES=3`) |
| Export/import config (JSON/XML) | DONE | `/config/export`, `/config/import` JSON; **XML missing** |
| Factory reset | DONE | `/reset` page, serial `factory`, BOOT button |
| Config backup to TFTP | **MISSING** | No TFTP backup |
| Standalone playback (presets/scenes) | **MISSING** | No preset/scene storage or playback |
| Multi-scene preset bank (≥32 scenes) | **MISSING** | No scene bank |
| Scene trigger (manual/network/GPI/timecode/schedule) | **MISSING** | No trigger system |
| Fade time per scene (in/out) | **MISSING** | No fade engine |
| Loop/sequence mode | **MISSING** | No sequencer |
| Scene priority vs live network | **MISSING** | No scene priority concept |
| Art-Net TimeCode (receive+send) | **MISSING** | No ArtTimeCode handling |
| Timecode-triggered scene playback | **MISSING** | Depends on above + scenes |
| DMX-in → Art-Net/sACN retransmit | **MISSING** | Only network→DMX, not DMX→network converter |
| Universe splitting (1 net → multiple ports) | **MISSING** | Each output has independent universe; no fan-out |
| Loopback / monitor universe | **MISSING** | No virtual loopback universe |
| LTP-Takeover merge mode | **MISSING** | Only HTP/LTP/OFF (3 modes, spec wants 4) |
| Priority-based merge mode | **MISSING** | Priority filtering exists in HTP/LTP but no explicit "priority-only" mode |

## Key Design Decisions

1. **Phase-based approach**: Implement in phases, each producing a buildable + testable increment. Phase 0 addresses missing core DMX features; Phase 1 network enhancements; Phase 2 presets/scenes; Phase 3 timecode/triggering; Phase 4 management/alerts.

2. **Schema-driven config**: All new persistent settings must be added to `OUTPUT_FIELDS[]` / `CONFIG_FIELDS[]` in `src/cfg/config_schema.cpp` so they are automatically NVS-persisted, serial-console-accessible, and web-form-compatible — following the existing pattern.

3. **Template-first defaults**: New board-independent defaults go in `templates/_base.ini`; board-specific overrides in `templates/luxdmx_4uni.ini`, `templates/luxdmx_v6.ini`, etc.

4. **Per-port vs global**: Features like failsafe timeout, break/MAB, polarity are per-output (extend `DmxOutput` struct + `OUTPUT_FIELDS[]`). Features like VLAN, NTP, syslog, SNMP are global (extend `Config` struct + `CONFIG_FIELDS[]`).

5. **DMX→network converter**: Requires adding DMX input capability. The current codebase has `uart_rx.h` for RDM RX only (RX-only UART). A DMX-in path needs a UART configured for DMX input (8N2, 250kbps) with break detection. This is a significant addition but fits the existing architecture (new `drv/dmx_input.cpp` + `core/input_router.cpp`).

6. **Preset/scene engine**: New `core/scene_engine.cpp` + `sys/scene_store.cpp` (NVS/Flash storage). Scenes are DMX snapshots with fade times. The existing `dmx_buffer.h` seqlock pattern protects frame data between the scene engine (core 0) and DMX tx task (core 1).

7. **Timecode**: Verified from Art-Net 4 spec (V1.4): `ARTNET_OP_TIMECODE = 0x9700`, `ARTNET_OP_TRIGGER = 0x9900`, `ARTNET_OP_COMMAND = 0x2400`. These are not currently defined in `artnet.h`. Opcode parsing is little-endian (`p[8] | (p[9] << 8)`), confirmed correct in `artnet.cpp:78`.

## Constraints & Preferences (from codebase)
- ESP32-S3 + W5500 + octal PSRAM, 4-universe board
- 5-layer architecture: `drv → cfg → core → net → app/sys`
- Core 0 = network, core 1 = DMX/RDM
- RMT-based DMX TX (no UART TX)
- Seqlock for core-to-core frame passing
- Schema-driven config (`config_schema.cpp` field table)
- Native host tests in `test/native/` with Arduino.h shim

## Implementation Plan

### Phase 0: Core DMX Completeness (missing per-port features)

#### Task 0.1: Configurable Break/MAB time per output
- Add `breakTime`, `mabTime` fields to `DmxOutput` struct (`include/config_schema.h:8`)
- Add `OINT_L("brk",...)` and `OINT_L("mab",...)` to `OUTPUT_FIELDS[]` in `config_schema.cpp`
- Add `dmx_break`, `dmx_mab` to `templates/_base.ini` with defaults (176us/12us)
- Modify `rmtDmxBuildLut()` and `g_breakWord` in `src/drv/dmx_rmt.h` to use runtime values instead of compile-time constants
- `RmtDmx` struct needs per-channel break/MAB params passed to encode function
- **Live reboot**: No — break/MAB can change live (encode uses runtime value); set `CFG_LIVE`

#### Task 0.2: DMX polarity inversion per port
- Add `invertPolarity` bool to `DmxOutput`
- Add `OBOOL("inv", ...)` to `OUTPUT_FIELDS[]`
- Modify `rmtDmxEncode()` in `dmx_rmt.h` to swap level 0/1 for each byte when inverted (invert the RMT symbol levels)

#### Task 0.3: Failsafe timeout + extended failsafe modes per port
- Add `failsafeTimeout` int (0–600) to `DmxOutput`
- Add `OINT_L("failst",...)` to `OUTPUT_FIELDS[]`
- Modify `merge_output.cpp:mergeOutput()` and `sender_tracker.cpp` — currently uses single global `SOURCE_TIMEOUT_MS=2500`; change to use per-output `cfg.outputs[i].failsafeTimeout * 1000` when > 0
- **Live**: Yes (`CFG_LIVE`)
- **Note**: Spec lists 4 failsafe modes: Hold Last Look / Go to Preset / Go Dark / Go to Home. Current code has Hold (0), Zero=Go Dark (1), Stop (2). Need to add: `LOSS_PRESET=3` (recall a specific scene preset) and `LOSS_HOME=4` (recall a "home" position preset). These tie into Phase 2's scene engine.

#### Task 0.4: LTP-Takeover and Priority-based merge modes
- Extend `config_enums.h` with `MERGE_LTP_TAKEOVER = 3` and `MERGE_PRIORITY = 4`
- Update `ENUM_MERGE` in `config_schema.cpp` labels
- Implement in `merge_engine.cpp`:
  - `MERGE_LTP_TAKEOVER`: Like LTP but a higher-priority source immediately preempts (not just "latest wins")
  - `MERGE_PRIORITY`: Only the highest-priority source(s) contribute, per-channel max (like HTP but restricted to top priority)

### Phase 1: Network Enhancements

#### Task 1.1: AutoIP (169.254.x.x) fallback
- Add `autoIpFallback` bool to `Config` (`CONFIG_FIELDS`)
- In `net_state.cpp`: when DHCP fails, if `autoIpFallback`, assign `169.254.x.x` via `WiFi.config()` with a generated link-local address
- Implement 169.254.x.x selection per RFC 3927 (probe before assign)

#### Task 1.2: QoS/DSCP marking
- Add `dscpEnabled` bool + `dscpDmx` int (0-63) to `Config`
- In `artnet.cpp` and `sacn.cpp`: set `IP_SET_TOS` / socket option `SO_REUSEADDR` replaced with IP_TOS with DSCP on the UDP sockets after creation

#### Task 1.3: VLAN tagging (802.1Q)
- Add `vlanEnabled` bool + `vlanId` int (1-4094) to `Config`
- Requires ESP-IDF `esp_eth` driver support for VLAN-tagged frames — **investigate feasibility on W5500 driver**. May need to use raw Ethernet frame API. **Risk**: This may require IDF-level changes beyond the Arduino framework.

#### Task 1.4: sACN Universe Discovery (extended — send own discovery)
- Current: consumes/discards discovery packets (`sacn.cpp:107-108`)
- Add: `startSacn()` periodically transmits sACN Universe Discovery announcement on a timer (every ~10s per E1.31 spec) for the set of universes this node receives
- Add `sacn_disc.cpp` module: builds the discovery packet (root layer + universe list) and sends to `239.255.255.222:5568` (the discovery multicast address per E1.31)

#### Task 1.5: DMX-in → Art-Net/sACN retransmit
- New file `src/drv/dmx_input.cpp`: UART configured as 8N2@250kbps with break detection via UART `UART_RXFIFO_FULL_EVT` + interrupt on break
- New file `src/core/input_router.cpp`: on DMX frame received, routes frame to network via `routeFrame()` (existing router) — node acts as converter
- Add `inputMode` enum to `DmxOutput`: `DMX_IN_OFF`, `DMX_IN_TO_NET`, `DMX_IN_MONITOR`
- Add to `OUTPUT_FIELDS[]`: `OINT_L("inmode",...)` mapping to input mode
- **Challenge**: Current RMT is TX-only per output. DMX-in needs a separate UART input path. Use `uart_rx.h` pattern but with break detection.

#### Task 1.6: Universe splitting + loopback/monitor
- Add `outputSplitMask` to `DmxOutput` — a bitmask of additional output indices that should receive the same universe frame (fan-out)
- In `frame_router.cpp:routeFrame()`, after matching the primary output, iterate the split mask and write the same frame to secondary outputs
- Add `loopbackUniverse` int to `DmxOutput` — when set, the output's frame is also written to a virtual universe (internal buffer) that can be monitored/retransmitted
- Add `monitorUniverse` int — read from a virtual universe and transmit on this physical output

### Phase 2: Presets & Playback

#### Task 2.1: Scene storage
- New file `src/core/scene_store.cpp` + header
- Store ≥32 scenes in a dedicated NVS namespace or SPIFFS/Flash partition
- Each scene: 512 bytes per universe × 4 = 2048 bytes + metadata (fade time, name)
- Add `sceneCount`, `sceneActive` to `Config`
- Scene struct: `name[32]`, `fadeTimeMs`, `triggerMask`, priority vs live

#### Task 2.2: Scene engine + fade engine
- New file `src/core/scene_engine.cpp`
- Fade engine: linear or S-curve interpolation from current frame to target frame over fade time
- Runs as a FreeRTOS timer or per-tick computation in `dmxTxTask`
- Scene priority vs live: when a scene is active and priority is higher than incoming network data, the scene overrides; when network priority is higher, scene fades out

#### Task 2.3: Scene triggers
- Manual: WebSocket command `{ "type": "scene", "play": <n>, "fade": <ms> }`
- Network: ArtTrigger opcode `0x9900` (verified from spec) — scene trigger payload
- GPI: GPIO pin state change triggers scene playback (configure via `config_schema.cpp`)
- Timecode: handled in Phase 3
- Schedule: simple time-of-day or uptime-based trigger (e.g., play scene at uptime+60s)

### Phase 3: Timecode & Sync

#### Task 3.1: Art-Net TimeCode receive + send
- Add `ARTNET_OP_TIMECODE = 0x9700` to `artnet.h` (verified from Art-Net 4 spec V1.4)
- Add TimeCode frame struct (hours, minutes, seconds, frames, type: SMPTE/Film/EBU; Drop-frame flag)
- In `artnet.cpp`: parse ArtTimeCode packets, store in global `g_timecode`
- Add `timecodeSend` config: enable sending ArtTimeCode at configured fps
- In `tasks.cpp` or a dedicated `timecode_task`: generate and send ArtTimeCode packets

#### Task 3.2: Timecode-triggered scene playback
- In `scene_engine.cpp`: check incoming timecode against configured scene triggers
- Scene trigger: "play scene N at timecode hh:mm:ss:ff" or "every N frames"

### Phase 4: Management & Monitoring

#### Task 4.1: Syslog (local + remote UDP)
- New file `src/sys/syslog.cpp`
- Forward ESP32 `Serial.println()` / `esp_log` output to a remote UDP syslog server
- Log levels: ERROR, WARNING, INFO, DEBUG
- Configurable syslog server IP + port + facility

#### Task 4.2: Email/webhook alerts
- On link down, source lost, etc.: send a webhook (HTTP POST) to a configured URL
- Email can be via a webhook-to-email provider (simpler than SMTP)
- New file `src/net/alerts.cpp`: `sendAlert(const char* msg)` using `HTTPClient`

#### Task 4.3: TFTP firmware update + config backup
- **TFTP update**: Extend `ota.cpp` to download firmware via TFTP (simple TFTP client on ESP32) — TFTP is used for simpler network boot scenarios
- **Config backup**: periodic backup of config JSON to a TFTP server — new file `src/net/tftp_backup.cpp`

#### Task 4.4: XML config export/import
- Extend `config_core.cpp:importJson`/`exportJson` to also support XML format
- XML schema: `<config><hostname>...</hostname><a_tx>17</a_tx>...</config>`

#### Task 4.5: Per-port packet loss stats
- Track per-output: frames received, frames dropped, last good frame timestamp
- Expose in `/health` and WebSocket frame
- Already have `txFrames` + `outFps`; add `rxFrameCount`, `rxLossCount`
- Modify `sender_tracker.cpp` / `frame_router.cpp` to increment stats on each routed frame

## Data Flow (new/modified paths)

```
Art-Net/sACN packet (core 0)
  → frame_router::routeFrame()
    → sender_tracker::updateSender()  [updates per-sender stats]
    → merge_engine::mergeOutput()     [HTP/LTP/priority/failsafe timeout]
      → dmx_buffer (seqlock write)
        → snapshotAndTransmit() in dmxTxTask (core 1)
          → rmtDmxKick() → RMT peripheral outputs DMX

New: DMX input (core 0)
  → dmx_input UART RX ISR
  → input_router → routeFrame()  [converts DMX-in to Art-Net/sACN retransmit]

New: Scene engine (triggered from loop or timecode)
  → scene_engine::render() → dmx_buffer (seqlock write)
  → merge with network sources based on priority
```

## Testing Strategy

1. **Native host tests** (`test/native/`): Add `scene_test.cpp`, `merge_extended_test.cpp` (for LTP-Takeover/Priority modes), `input_router_test.cpp`
2. **Hardware test script** (`test/hardware/test_4output_load.py`): Already exists for basic 4-output; extend to test split/fan-out
3. **Build verification**: `pio run -e esp32s3_n16r8_eth` must compile after each phase

## Risks & Mitigations

| Risk | Mitigation |
|---|---|
| VLAN tagging requires IDF-level driver changes on W5500 | Investigate ESP-IDF `ETH` API for VLAN support; if not feasible at Arduino layer, implement as "planned but blocked" and document the limitation |
| TFTP client adds code size | Use minimal TFTP implementation, only compile when `HAS_OTA_TFTP` defined |
| DMX-in break detection on UART is timing-sensitive | Use RMT for DMX input too (RMT can receive with edge detection, same timing as TX) — `src/drv/dmx_rmt.h` already uses RMT; consider RMT RX for input path |
| Scene fade engine adds CPU load on core 1 | Use simple linear interpolation (fast), or offload to a low-priority timer |
| Art-Net opcodes: Verified from Art-Net 4 spec (V1.4). `ARTNET_OP_TIMECODE = 0x9700`, `ARTNET_OP_TRIGGER = 0x9900`, `ARTNET_OP_COMMAND = 0x2400`. These are not currently defined in `artnet.h`. Opcode parsing is little-endian (`p[8] | (p[9] << 8)`), confirmed correct in `artnet.cpp:78`. Need to add TIMECODE + TRIGGER to `artnet.h`. |

## Order of Implementation

1. Phase 0 (Tasks 0.1–0.4): Completes core DMX per-port config — highest priority, lowest risk
2. Phase 1 (Tasks 1.1–1.2, 1.4): Network enhancements — AutoIP, DSCP, sACN discovery (send) — moderate risk
3. Phase 1 (Task 1.5): DMX-in converter — high value, needs RMT RX investigation
4. Phase 2 (Tasks 2.1–2.3): Presets/playback — foundational for many use cases
5. Phase 3 (Tasks 3.1–3.2): Timecode — depends on understanding scene triggers
6. Phase 4 (Tasks 4.1–4.5): Management/monitoring — lowest priority, often deploy-specific

## Open Questions

1. **VLAN feasibility**: Can the ESP32 Arduino W5500 driver support 802.1Q tagged frames? If not, should we use raw Ethernet frame injection via ESP-IDF, or mark as future?
2. **TFTP update necessity**: The spec calls for TFTP firmware update, but the existing HTTP GitHub OTA + URL OTA is more capable. Is TFTP strictly required, or is HTTP sufficient for the use cases?
3. **Art-Net opcodes**: Verified from Art-Net 4 spec (V1.4). `ARTNET_OP_TIMECODE = 0x9700`, `ARTNET_OP_TRIGGER = 0x9900`. `ARTNET_OP_COMMAND = 0x2400` for text commands. Opcode parsing is little-endian (`p[8] | (p[9] << 8)`), confirmed correct in `artnet.cpp:78`. Need to add these to `artnet.h`.

## Validation Plan

- Each phase: `pio run -e esp32s3_n16r8_eth` builds cleanly
- Native host tests: `cd test && python build/test_native.py` (or equivalent) passes
- Hardware: 4-universe simultaneous at 40fps, merge modes verified with two sources
- Network: Art-Net controller (sACN, Avolites, Chamsys, MA) interoperability
- RDM: discovery + PIDs verified with actual fixtures

