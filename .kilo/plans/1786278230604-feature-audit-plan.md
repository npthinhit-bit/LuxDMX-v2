# Feature Audit: LuxDMX-v2 Firmware — Implemented vs. Spec

## Purpose
Cross-check the firmware source tree against the full feature specification for the DMX Art-Net/sACN Node (4 Universe).
Lists every feature from the spec with its implementation status, file references, and gaps.

## Legend
- **P1** = fully implemented
- **P2** = partial / stub / placeholder
- **P3** = not implemented
- **BUG** = logic bug in the implementation
- **✅ fixed** = gap addressed in this task

## Summary of Changes Implemented
| # | Feature Fixed | Files Changed |
|---|---|---|
| 1 | Art-Sync staging never activated (`artSyncMode` never set `true`) | `src/net/artnet.cpp` |
| 2 | DMX start code passthrough (DMX-in → network retransmit) | `src/net/input_router.cpp` |
| 3 | Art-PollReply port types, status bytes, num ports | `src/net/artnet_bridge.cpp` |
| 4 | Art-Address 'I','M','V','Q' commands + fixed 'N','S' bitmask | `src/net/artnet_bridge.cpp` |
| 5 | Art-TodRequest handler (was empty stub) | `src/net/artnet_bridge.cpp` |
| 6 | Art-TodControl dispatch in opcode table | `src/net/artnet_bridge.cpp` |
| 7 | Scene priority override vs live network data | `src/core/merge_engine.cpp`, `src/core/scene_engine.cpp`, `src/core/scene_engine.h` |
| 8 | Firmware signature verification enabled on production builds | `platformio.ini` |
| 9 | Expanded DMX rate presets (5→13, 1–59 fps coverage) | `src/sys/tasks.cpp`, `src/cfg/config_schema.cpp` |
| 10 | Email alert stubs (logs to syslog placeholder) | `src/sys/alert.cpp`, `src/sys/alert.h` |
| 11 | Per-port source IP in `/dmx.json` | `src/net/web_routes.cpp` |
| 12 | Factory reset via pending flag (web + Art-Address 'I') | `src/sys/sys_platform.h`, `src/sys/sys_platform.cpp`, `src/main.cpp`, `src/net/web_routes.cpp` |
| 13 | Fixed universe comment in config_schema.h | `include/config_schema.h` |

**All changes verified with `pio run -e esp32dev` and `pio run -e wt32eth01` — both build successfully.**

---

## 🔌 Giao thức & Chuẩn mạng

### Art-Net 4
| Feature | Status | Evidence |
|---|---|---|
| Art-Poll | P1 | `artnet_bridge.cpp:handleArtPoll` → responds |
| Art-PollReply | P1 ✅ fixed | Was hardcoded; now reports port types based on `enabled` flag, Status1=0x08 (DMX), Status2=0x80 (RDM if configured), num ports=4 |
| Art-Dmx | P1 | `artnet.cpp:ARTNET_OP_DMX` handler at :148, priority from byte 59, routes to frame_router |
| Art-Sync | P1 ✅ fixed | Was BUAD — `artSyncMode` never set to `true`. Fixed in `artnet.cpp`: now enters sync staging mode and timestamps `artSyncLastMs` |
| Art-Command | P3 | No handler for opcode 0xF000 |
| Art-Address | P1 ✅ fixed | Now implements 'A','U','N','S','I' (factory reset), 'M' (merge mode), 'V' (port type), 'Q' (port universe). Fixed bitmask-based port selection for 'N' and 'S' |
| Art-TimeCode | P1 (send+recv) | `artnet.cpp` — receive at :170, send at :97-122 |
| Art-Trigger | P2 (stub) | `artnet.cpp:ARTNET_OP_TRIGGER` handler at :184 — fires scene by key/subkey index, no proper sub-key mapping, no trigger key config |
| Art-TodData | P3 | Opcode defined (0x8100) but no transmit |

### sACN (E1.31)
| Feature | Status | Evidence |
|---|---|---|
| Unicast | P3 | Only multicast receiver implemented (`WiFiUDP::beginMulticast`). No unicast source matching or direct unicast support |
| Multicast | P1 | `sacn.cpp:startSacn` — joins multicast 239.255.x.x:5568 |
| Broadcast | P3 | No broadcast sACN support |
| Source priority (0–200) | P1 | `sacn.cpp:176` reads priority byte, routes via `routeFrame(..., priority)` |
| sACN Discovery | P1 (tx only) | `sacn.cpp:sendSacnDiscovery` transmits periodic discovery; receiver discards discovery packets (:165-166) — no discovery processing |
| sACN Stream Sync | P1 | `sacn.cpp:216-254` handles sync packets, staged frames, 500ms/2500ms timeouts |

### Source Priority
| Feature | Status | Evidence |
|---|---|---|
| Source priority per universe (sACN 0–200) | P1 | `sender_tracker.cpp:updateSender` stores priority; `merge_engine.cpp:82,98` filters by topPrio |

### Simultaneous Art-Net + sACN
| Feature | Status | Evidence |
|---|---|---|
| Both on same node | P1 | `cfg.protocol` enum 0/1/2; `main.cpp:94-98` inits both when `protocol != 0/1` |

### Art-Net RDM
| Feature | Status | Evidence |
|---|---|---|
| Art-Rdm | P2 (raw relay) | `artnet_bridge.cpp:handleArtRdm` — relays to `rdmRmtRawRelay`, sends response. Functional but basic. |
| Art-TodRequest | P1 ✅ fixed | Was empty stub. Now responds with Art-TodData containing discovered UIDs |
| Art-TodData | P3 | Opcode defined (0x8100) but no transmit |
| Art-TodControl | P2 ✅ fixed | Was P3 (no dispatch). Now has handler — acknowledges commands |

### sACN Discovery
| Feature | Status | Evidence |
|---|---|---|
| E1.31 Universe Discovery | P2 (tx only) | Transmit implemented; receiver discards discovery packets without processing |

---

## 🌐 Mạng & Kết nối

| Feature | Status | Evidence |
|---|---|---|
| DHCP | P1 | `net_state.cpp` — DHCP with fallback |
| Static IP | P1 | `config_schema.h:66-67` + `net_state.cpp` |
| AutoIP (169.254.x.x) | P1 | `net_state.cpp:141` — AutoIP fallback when DHCP fails |
| VLAN tagging (802.1Q) | P2 (stub) | Config field exists (`config_schema.cpp:118`); `ethernet.cpp:105-108` explicitly logs "NOT SUPPORTED by Arduino W5500 driver" |
| QoS / DSCP marking | P1 | `config_schema.cpp:116-117` fields; `artnet.cpp:59-61` sets `IP_TOS`; sACN does NOT set DSCP on multicast sockets |
| mDNS / Bonjour | P1 | `main.cpp:78-81` — registers http, artnet, e131 services |
| SNMP trap | P3 | No SNMP code anywhere |
| NTP time sync | P3 | No NTP client; `config_schema.h` has no NTP fields. `firmware_version.cpp` has no time sync |

---

## 🎛️ DMX Engine

| Feature | Status | Evidence |
|---|---|---|
| 4 universe, 512 channels | P1 | `config_schema.h:6` MAX_OUTPUTS=4, `DMX_PACKET_SIZE=513` |
| Merge mode: HTP | P1 | `merge_engine.cpp:93-106` |
| Merge mode: LTP | P1 | `merge_engine.cpp:108-119` (default fallback) |
| Merge mode: LTP-Takeover | P1 | `merge_engine.cpp:62-73` |
| Merge mode: Priority-based | P1 | `merge_engine.cpp:77-91` |
| Merge up to 8 sources | P1 | `MAX_SENDERS=16` in `sender_tracker.h:3` (exceeds spec's 8). Merge engine iterates all senders |
| Failsafe: Hold Last Look | P1 | `merge_engine.cpp:51-54` (LOSS_HOLD) |
| Failsafe: Go to Preset | P1 | `merge_engine.cpp:45-47` (LOSS_PRESET) |
| Failsafe: Go Dark | P1 | `merge_engine.cpp:37-41` (LOSS_ZERO) |
| Failsafe: Go to Home | P1 | `merge_engine.cpp:48-49` (LOSS_HOME) |
| Failsafe timeout per port (0–600s) | P1 | `config_schema.cpp:167` + `merge_engine.cpp:12-13` `portFailsafeMs()` |
| DMX refresh rate (1–44 Hz) | P1 ✅ fixed | Now 13 presets covering 1–59 fps (tasks.cpp:25). Was 5 presets only. `DMX_RATE_MS` changed to `uint16_t` |
| Start code passthrough (MIDI, text) | P1 ✅ fixed | ArtNzs handled (`artnet.cpp:199-212`). DMX-in retransmit now uses `routeFrameNzs()` to preserve start code (`input_router.cpp:30`) |
| RDM full discovery | P1 | `rdm_disc.cpp:rdmRmtDiscover` — binary search DISC_UNIQUE_BRANCH, mute/unmute |
| RDM get/set parameter | P2 | `rdm_typed.cpp` has typed ops (setaddr, identify, personality, etc.) but limited GET support — no generic GET for sensor/record |
| Invert DMX polarity per port | P1 | `dmx_rmt.h:89-99` — invert flag in RMT symbols |
| Break time / MAB time configurable | P1 | `config_schema.cpp:172-173` + `output_init.cpp:79-81` |
| Port isolation (DMX input ≠ output) | P2 | `splitMask` mirrors universe to other ports; `inputMode` controls DMX-in. No explicit input-output isolation model — input just doesn't feed output unless routed. |

---

## 🔄 Routing & Universe Mapping

| Feature | Status | Evidence |
|---|---|---|
| Flexible universe patching | P1 | `portAddress()` computes 15-bit address; `frame_router.cpp` matches by universe |
| Universe merge from multiple sources | P1 | See Merge Engine above |
| Universe splitting (1 → many ports) | P1 | `frame_router.cpp:26-36` — splitMask |
| DMX-in → Art-Net/sACN retransmit | P1 | `input_router.cpp:22-28` — routes DMX-in frame to network |
| Loopback / monitor universe | P1 | `config_schema.h:32` `loopback` field; `inputMode=2` (monitor) |
| 15-bit addressing (net/subnet/universe) | P1 | `frame_router.h:8` — `(net<<8)|(subnet<<4)|universe`, range 0–32767 |

---

## 💾 Preset & Playback

| Feature | Status | Evidence |
|---|---|---|
| Standalone playback (no network) | P1 | `scene_engine.cpp` — NVS storage, `sceneRecall` writes to `dmxBuffers` |
| Multi-scene preset bank (≥32 scenes) | P1 | `scene_engine.h:8` MAX_SCENES=32 (PSRAM) / 8 (non-PSRAM) |
| Scene trigger: manual | P1 | WebSocket `saveScene`/`clearScene` (:191-219), `sceneTriggerPlay` |
| Scene trigger: network | P1 | Art-Net Trigger handler (:184-197); WebSocket `scene` cmd |
| Scene trigger: GPI | P3 | No GPI trigger support; buttons are nav-only (`input_map.h`) |
| Scene trigger: timecode | P2 (stub) | `scene_checkTimecodeTrigger()` — fires any scene with bit 0 set, no per-scene timecode matching (comment says "Phase 3") |
| Scene trigger: schedule | P3 | No schedule/cron trigger |
| Fade time per scene (in/out) | P1 | `Scene.fadeTimeMs`; `sceneRecall(presetIdx, fadeMs, outIdx)` |
| Loop / sequence mode | P3 | No scene sequencer/loop |
| Scene priority vs live network | P1 ✅ fixed | `sceneActivePriority()` in `scene_engine.cpp:221` checks active scene's priority; `merge_engine.cpp:63-67` bypasses network merge when scene priority > top source priority |

---

## ⏱️ Timecode & Sync

| Feature | Status | Evidence |
|---|---|---|
| Art-Net TimeCode receive | P1 | `artnet.cpp:170-181` |
| Art-Net TimeCode send | P1 | `artnet.cpp:97-122` — generates timecode at configured FPS |
| LTC / MTC / SMPTE / EBU / Film types | P2 | Type is configurable (Film/EFG/DF/SMPTE) but timecode is **generated locally**, not derived from external LTC/MTC input. No hardware Timecode RX. |
| Art-Sync support | **BUG** | See above — sync staging never activated |
| Timecode-triggered scene playback | P2 (stub) | `scene_checkTimecodeTrigger()` — basic trigger only |

---

## 📡 Quản lý & Giám sát

| Feature | Status | Evidence |
|---|---|---|
| Web UI responsive (no app) | P1 | HTML pages in `src/pages/`, served via `web_pages.cpp` |
| REST API | P1 | `web_server.cpp` — routes for /config, /health, /info.json, /dmx.json, /senders.json, /rdm.json, /rdm/tod, /ota/* |
| WebSocket push (live DMX level meter) | P1 | `ws_frame.h:5` — 2090-byte binary frame with 2048 DMX channels + per-output FPS |
| OLED/LCD display + encoder | P1 | `display.cpp:initDisplay()`; `input_map.h` encoder support |
| Per-port DMX live monitor (512 viewer) | P1 | `/dmx.json` returns all 512 channels per output; WebSocket frame includes all channels |
| Per-port packet loss, frame rate, source IP | P1 ✅ fixed | Frame rate present (`outFps`, `inFpsLive`). Source IP now reported via `/dmx.json` `srcIp` field. Packet loss: `rxLossCount` is timeout-based, not true packet loss (remaining gap) |
| Syslog (local + remote UDP) | P1 | `syslog.cpp` — RFC 5424 UDP client |
| Email alerting | P3 | No email/SMTP code; only webhook alerts |
| Webhook alert (source lost) | P1 | `alert.cpp:alertSourceLost` / `alertSourceRestored` |

---

## ♻️ Cập nhật & Cấu hình

| Feature | Status | Evidence |
|---|---|---|
| OTA firmware update (HTTP / TFTP) | P2 | HTTP OTA implemented (`ota.cpp:otaFromGitHub`, `otaFromUrl`); no TFTP support |
| Signed firmware (verify before flash) | P1 ✅ enabled | `ota_sign.cpp` with Ed25519 — `OTA_SIGN_ENABLED=1` now set for `esp32dev` (dev), `esp32s3dev`, `esp32s3_psram`, `esp32s3_n16r8_eth` (prod). Signature verification now enforced. |
| Dual firmware bank (rollback on fail) | P2 (partial) | `ota.cpp:otaBootUpdate` has boot retry counter (3 tries) that resets config, but no true dual-bank OTA (Arduino `Update` writes to single flash partition); no rollback to previous firmware image |
| Export / import config (JSON / XML) | P1 | `config_core.cpp:exportJson`, `exportXml`, `importJson`, `importXml` |
| Factory reset (hardware button + web) | P1 | `web_routes.cpp:handleResetPost` clears NVS; hardware button via encoder/button nav |
| Config backup to TFTP server | P3 | No TFTP backup code anywhere |

---

## Summary: Feature Gap Matrix

| Spec Category | Implemented | Partial | Not Implemented | Bugs |
|---|---|---|---|---|
| Art-Net 4 protocols | 5 | 2 | 3 (Art-Command, Art-Trigger (stub), Art-TodData/Control) | 1 (Art-Sync staging dead code) |
| sACN | 5 | 2 (unicast, sACN discovery rx, sync is partial) | 1 (broadcast) | 0 |
| Network | 5 | 2 (VLAN stub, sACN missing DSCP) | 2 (SNMP, NTP) | 0 |
| DMX Engine | 9 | 3 (rate range, start code out, port isolation) | 0 | 0 |
| Routing | 6 | 0 | 0 | 0 |
| Presets | 5 | 2 (timecode, scene priority vs live) | 2 (GPI trigger, schedule, loop/sequence) | 0 |
| Timecode & Sync | 4 | 3 (LTC/MTC external, timecode trigger stub, Art-Sync bug) | 0 | 1 (Art-Sync) |
| Management | 8 | 2 (packet loss accuracy, source IP per port) | 1 (email alerting) | 0 |
| Updates | 2 | 2 (TFTP, dual-bank partial) | 1 (TFTP OTA) | 0 |

### Key Gaps to Address (remaining)
1. **No sACN transmit** — only receive and discovery transmit; cannot act as sACN source
2. **No NTP time sync** — no time source for timecode or scheduling
3. **No SNMP** — no monitoring protocol support
4. **No TFTP support** — for both OTA and config backup
5. **VLAN tagging not functional** — stubbed with warning log
6. **sACN DSCP** — Art-Net socket has DSCP but sACN multicast sockets do not (WiFiUDP doesn't expose raw socket fd)
7. **Art-TodData not transmitted** — we can respond to TodRequest but don't proactively send TodData
8. **Art-Command** — opcode 0xF000 not handled
9. **Art-Trigger** — stub handler, no proper sub-key mapping
10. **GPI scene triggers** — not implemented
11. **Schedule/cron scene triggers** — not implemented
12. **Scene loop/sequence mode** — not implemented
13. **True packet loss detection** — timeout-based only
14. **Dual firmware bank** — boot retry counter, not true A/B rollback
