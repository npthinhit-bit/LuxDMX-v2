# LuxDMX-v2 Professional Roadmap Implementation Plan

## Goal
Implement the LuxDMX Professional Firmware Roadmap (Phase 0-4) on the ESP32-S3-WROOM-2 N16R8 + W5500 4-universe board, targeting professional show-floor protocol compliance.

## Constraints & Preferences
- ESP32-S3-WROOM-2 N16R8 + W5500 + octal PSRAM, 4-universe board (2 RDM-capable + 2 DMX-only)
- Architecture: 5-layer (drv → cfg → core → net → app/sys), core 0 = network, core 1 = DMX/RDM
- RMT-based DMX transmission using `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL` for all DMA buffers
- Seqlock pattern for core-to-core frame passing (`dmx_buffer.h`)
- Schema-driven config (`config_schema.cpp` field table, NVS persistence)
- Native host tests in `test/native/` using `Arduino.h` shim and `build/test_native.py`
- No hardware changes allowed; all firmware features

## Progress

### Sprint 1: v2 Modularization (COMPLETE)
- **1.1 Tasks**: Implemented real task bodies — `dmxTxTask` (core 1, prio 19), `netRxTask` (core 0, prio 5), `ledTask`, `displayTask`, `versionCheckTask`. Added crash-recovery init guard (Preferences `dmxgw` namespace, progressive output disable).
- **1.2 Art-Net**: Full opcode dispatch — ArtPoll/ArtPollReply, ArtAddress (net/sub/uni), ArtIpProg, ArtTod*, ArtRdm relay → DMX bus via `rdmRmtRawRelay`, ArtSync staging, ArtNzs pass-through. Bridge dispatch in `artnet_bridge.cpp`.
- **1.3 sACN**: Priority byte parsing, Universe Discovery (consume/discard), Stream Sync staging with sync-address timeout. Full port-address multicast group binding.
- **1.4 Web routes**: All 501 stubs replaced — /dmx.json, /senders.json, /log.json, /info.json, /version.json, /rdm.json, /config (GET+POST), /config/export, /config/import, /health, /setup/scan, /setup, /reset, /reboot, /ota/github, /ota/url, /ota/upload, /rdm/*, /led/bright.
- **1.5 WebSocket**: `handleWsText()` with viewout/blackout/mode/identify/set commands. `handleWsTextRdm()` with discover/setaddr/identify/setpers/setlabel. Queued operations processed via `rdmWsProcessQueued()` from loop().
- **1.6 OTA**: HTTP upload via ESPAsyncWebServer, GitHub release OTA, URL OTA, boot-update rollback with crash counter. Ed25519 signature verification framework (enabled in production via `OTA_SIGN_ENABLED`).

### Sprint 2: Phase 0 - 4-output Foundation (COMPLETE)
- Soak monitor task with DRAM/PSRAM heap tracking + auto-reboot (<30KB DRAM). `LUXDMX_SOAK_TEST` build flag on esp32s3_n16r8_eth.
- Hardware load test script (`test/hardware/test_4output_load.py`) — drives all 4 universes at 40fps.
- DRAM audit: verified RMT buffers use `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`. Channel 3 on ESP32-S3 does NOT have DMA (comment confirmed at `dmx_rmt.h:101`).
- 4-output template updated with net/subnet/sacnUniverse fields.
- `build/test_native.py` created — compiles native tests with Arduino.h shim.

### Sprint 3: Phase 1 - Protocol Compliance (IN PROGRESS)
- **ArtSync staging**: ArtDMX frames staged in `dmxStaged[]` until ArtSync arrives; flushed by DMX task. Timeout fallback (1s).
- **Full port-address model**: 15-bit `(net << 8) | (subnet << 4) | universe` matching across artnet, sacn, frame_router, sender_tracker, merge_engine.
- **ArtNzs pass-through**: routeFrameNzs() handles non-zero start codes.
- **sACN Stream Sync**: sync-address parsing with staged buffers.
- **sACN Universe Discovery**: extended frame vector 0x00000004 handled.
- **ArtPollReply**: proper net/subnet/universe encoding per output.
- **ArtAddress**: 'A' (all), 'U' (unlock/commit), 'N' (net), 'S' (subnet), 'T' (test).

### Sprint 4: Phase 2 - Extended RDM
- Extended PID definitions in `rdm_types.h`: DEVICE_MODES, DEVICE_MODE, IDENTIFY_MODE, BURN_IN, DEVICE_HOURS, DEVICE_POWER, PERSONALITY_DESCRIPTION, SENSOR_RECORD.
- `rdmSubDeviceCount()` for sub-device enumeration (opt-in, cap 8).

### Sprint 5: Phase 3 - Production Features
- OTA signature verification framework (`ota_sign.h/cpp`) with Ed25519 public key.
- Config export/import JSON endpoints.
- Health endpoint with per-output status.

### Sprint 6: Console Interop (PENDING)
- Verify serial console commands work with new config schema.

## Key Decisions
- v2 modularization completed before Phase 0-4 (stubs → real implementations)
- ArtSync uses DRAM staging buffers (`dmxStaged[]`) with `artSyncMode` flag
- sACN universe = `sacnUniverse` field (default 0 = auto = universe+1)
- Sub-device enumeration opt-in (default cap 8) via `rdmMaxDev` config
- ArtNzs pass-through off by default (safety-first)
- OTA signing: Ed25519 verification, dev builds skip (`OTA_SIGN_ENABLED=0`)
- Web UI auth: off by default, write endpoints unprotected for dev
- DMX tx task on core 1 at prio 19; net rx task on core 0 at prio 5
- RMT channel 3 does NOT have DMA on ESP32-S3 — verified at `dmx_rmt.h:101`

## Next Steps
1. Build verification: `pio run -e esp32s3_n16r8_eth` — compiles cleanly
2. Hardware flash test: verify 4 outputs at 40fps
3. ArtNet controller interoperability: sACN, Avolites, Chamsys, MA
4. RDM discovery: sub-device enumeration test with fixtures
