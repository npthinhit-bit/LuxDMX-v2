# Firmware Evaluation Checklist — Full Remediation Plan

## Goal

Fix all 23 issues in `FIRMWARE_EVALUATION_CHECKLIST.md`, pass the T01–T10 test matrix, and update `CLAUDE.md` with the firmware evaluation workflow (WiFi "MSI"/"12345678" → compile `esp32s3_psram` → flash via COM7 → monitor 5 min → verify `dmx-gateway.local` → proceed to full project compile).

## Constraints & Preferences
- ESP32-S3-WROOM-2 N16R8 + W5500 + octal PSRAM target hardware
- 5-layer architecture: drv → cfg → core → net → app/sys, core 0 = network, core 1 = DMX/RDM
- Core-pinning is critical: AsyncTCP on core 0, RDM/DMX on core 1
- Native host tests use custom shim framework in `test/native/` (not Unity — see Issue #10)
- Templates live in `templates/*.ini`; config schema in `src/cfg/config_schema.cpp`

## Current State Assessment

### Already Resolved (verify & mark complete)
- **Issue #1 (RDM Transaction Blocks DMX Task)**: FIXED. `src/core/rdm_task.cpp` implements a dedicated RDM task (core 1, priority 18) with a command queue. Blocking wrappers (`rdmTransaction()`, `rdmRmtDiscover()`, etc.) now dispatch via `xQueueSend` + semaphore. The DMX TX task is not blocked.
- **WiFi credentials "MSI"/"12345678"**: Already configured in `templates/esp32s3_psram.ini` (lines 6-7).
- **Hostname `dmx-gateway`**: Already set in `templates/_base.ini` (line 9). `MDNS.begin(cfg.hostname.c_str())` is called in `src/main.cpp:79`. The `.local` mDNS resolution is already functional.
- **mDNS service registration**: Already in `src/main.cpp:80-82` (http/tcp:80, artnet/udp:6454, e131/udp:5568).

### Partially Addressed
- **Issue #4 (sACN Sync)**: Sync staging exists (`sacn.cpp:183-221`), sync packet commit exists (`228-255`), 500ms grace (`218`), 2.5s timeout (`219`). Needs per-output sync universe verification and simultaneous commit test.
- **Issue #5 (WebSocket Push)**: Already uses binary frames + static buffer reuse (`src/net/ws_frame.h`). No delta encoding or per-universe subscription yet.

### Issue #19 (ArtIpProg) — Clarification Needed
- `ipProg` config field defaults to `false` (neutral value for bool). `handleArtIpProg()` is a minimal handler that only sets `g_artCfgDirty`. The checklist says "enabled by default" but the current code defaults to disabled. **Verify whether this is already addressed or if stricter enforcement (auth token check) is still needed.** For now, plan assumes the field-level default is sufficient but adds auth token enforcement.

## Phase 1: Critical Issues (Issues #2–7)

### P1.1 — Issue #2: Art-Net/sACN RX Loops Starve Core-0

**Current state**: `artRdmPollRx()` loops 64× per call (`src/net/artnet.cpp:80`); `readSacnSocket()` loops 16× per call (`src/net/sacn.cpp:144`). Both called from `netRxTask` (core 0, 2ms period).

**Plan**:
1. Create a lock-free ring buffer for parsed Art-Net packets (`src/net/art_pkt_queue.{h,cpp}`):
   - Capacity: 32 packets, each up to 640 bytes
   - `artRdmPollRx()` becomes a producer: reads at most 8 packets per call, pushes parsed packets to the ring buffer
   - `netRxTask` consumer loop: dispatches from the ring buffer
2. Create similar ring buffer for sACN (`src/net/sacn_pkt_queue.{h,cpp}`):
   - Capacity: 16 packets
   - `readSacnSocket()` reads at most 4 packets per call, pushes to queue
3. Reduce loop bounds: Art-Net 64→8, sACN 16→4. The ring buffer absorbs bursts between 2ms ticks.
4. Acceptance: `netRxTask` CPU < 20% under sustained 100 fps Art-Net + sACN load (measured via `esp_timer_get_cycle_count` or FreeRTOS `uxTaskGetSystemState`).

**Files to modify**: `src/net/artnet.cpp`, `src/net/artnet.h`, `src/net/sacn.cpp`, `src/net/sacn.h`, `src/sys/tasks.cpp`

### P1.2 — Issue #3: Art-Net RDM Response Queue

**Current state**: `artRdmDrainResponses()` is a stub (`src/net/artnet.cpp:91-95`). ArtRDM responses are currently sent synchronously inline in `handleArtRdm()` (`src/net/artnet_bridge.cpp:111-138`), which works but doesn't use a queue.

**Plan**:
1. Add a ring buffer in `artnet.h/cpp` for RDM responses: `ArtRdmResp respQueue[8]` with head/tail indices
2. Modify `handleArtRdm()` to push the response to the ring buffer instead of sending inline
3. Implement `artRdmDrainResponses()` to pull from the ring buffer and `lwip_sendto()` each response
4. Call `artRdmDrainResponses()` from `loop()` or `netRxTask` on core 0
5. Acceptance: Art-Net controller receives `GET_DEVICE_INFO` reply via ArtRdm — test with QLC+ or Lightkey

**Files to modify**: `src/net/artnet.h`, `src/net/artnet.cpp`, `src/net/artnet_bridge.cpp`

### P1.3 — Issue #4: sACN Sync Universe (verify + complete)

**Current state**: Sync staging and commit already exist. Verify the full E1.31 sync flow.

**Plan**:
1. Verify per-output sync universe membership: each output's `sacnSync` field is used to join the correct sync multicast group (`sacn.cpp:130-135`)
2. Verify sync packet commit path (`sacn.cpp:232-255`) triggers `routeFrame()` for all outputs on the same sync universe simultaneously
3. Verify 500ms commit grace (`sacn.cpp:218`: `syncMs < 500`) and 2.5s timeout (`sacn.cpp:219`: `syncMs >= 2500`)
4. Add test: 4 outputs on sync universe commit simultaneously within 1 frame (<4ms jitter)
5. Acceptance: T05 test passes

**Files to verify/modify**: `src/net/sacn.cpp`, `src/core/frame_router.cpp`

### P1.4 — Issue #5: WebSocket Push Optimization

**Current state**: Binary frames with static buffer reuse (good). Pushes every 100ms with ~2090-byte frame. No delta encoding.

**Plan**:
1. Add per-client subscription model:
   - `ws_handler.cpp`: parse `{"cmd":"subscribe","universes":[0,1]}` — client only receives data for subscribed universes
   - Track subscription mask per `AsyncWebSocketClient` in a map
2. Add delta encoding for DMX data:
   - Compare current frame to last sent frame per client; only transmit changed channels
   - For subscribed universes, send partial frame with channel range markers
3. Add a `WS_FRAME_CHANGED` bitmap in the frame header indicating which universes have data
4. Keep meta push (per-output fps, heap, uptime) at 2 Hz as-is (already bounded)
5. Acceptance: 10 WS clients × 10 Hz < 5% CPU, no heap fragmentation after 24h — T10 test

**Files to modify**: `src/net/ws_frame.h`, `src/net/websocket.cpp`, `src/net/ws_handler.cpp`, `src/net/websocket.h`

### P1.5 — Issue #6: NVS Scene Key Collision

**Current state**: `sceneKey(idx, chunk)` returns `"s{idx}c{chunk}"` with no namespace prefix (`src/core/scene_engine.cpp:53-55`). The scene meta key is `"s{idx}m"`.

**Plan**:
1. Prefix scene keys: `"scn_s{idx}c{chunk}"` and meta: `"scn_s{idx}m"`
2. Add NVS migration in `src/cfg/nvs_migrate.cpp`: rename old `s{idx}c{*}` keys to `scn_s{idx}c{*}` and `s{idx}m` to `scn_s{idx}m`
3. Update `sceneKey()` and `sceneMetaKey()` in `scene_engine.cpp`
4. Acceptance: NVS dump shows only prefixed scene keys; import/export round-trip works

**Files to modify**: `src/core/scene_engine.cpp`, `src/cfg/nvs_migrate.cpp`

### P1.6 — Issue #7: DMX Input Break Detection

**Current state**: `dmxInPoll()` uses a 2ms inter-byte timeout to detect frame boundaries (`src/drv/dmx_input.cpp:43`). The comment says it's fragile on UART overrun.

**Plan**:
1. Enable UART pattern detection on the input UART: `uart_enable_pattern_detect()` with `UART_PATTERN_LEN_MIN=88`, `UART_PATTERN_PRESCALER` set for break detection. The UART break is a line low for ≥88µs; the pattern detector fires on a configurable low-time threshold.
2. Alternative if pattern detect doesn't work: use RMT RX peripheral which can timestamp break/MAB with hardware-level accuracy
3. `dmxInPoll()` → detect pattern match event instead of timeout:
   - `uart_get_pattern_detect_length()` returns the break duration
   - Reset frame start on pattern match
4. Keep the inter-byte timeout as fallback (not primary)
5. Acceptance: Scope-verified break detection at 250kbaud with injected noise — T01 scope verification also covers this

**Files to modify**: `src/drv/dmx_input.cpp`, `src/drv/dmx_input.h`

## Phase 2: High Priority (Issues #8–11)

### P2.1 — Issue #8: Global State Proliferation

**Current state**: 40+ `extern`/`static` globals across headers (confirmed: `artnet.h`, `dmx_buffer.h`, `stats.h`, `rdm_engine.h`, `net_state.h`, etc.)

**Plan**:
1. Create opaque context structs per module:
   - `ArtnetCtx` (replaces `g_artSock`, `g_nodeIp`, `g_nodeMac`, `g_artPolls`, `artSyncMode`, etc.)
   - `NetStateCtx` (replaces `g_apMode`, `g_useEth`, `g_apWiredFallback`, `g_setupPortal`)
   - `RdmCtx` (replaces `g_rdm`, `identifyCh`, `identifyUntil`, `rdmPollDirty`)
   - `StatsCtx` (replaces `frameCount`, `fps`, `inFrameCnt[]`, etc.)
   - `SacnCtx` (replaces `sacnUdp[]`, `sacnBuf`, `sacnSyncAddress[]`, etc.)
2. Pass `*Ctx` pointer to each module's init/function calls
3. Remove `extern` from headers — expose only functions
4. Keep `Preferences` and config in `cfg` layer (already encapsulated)
5. Acceptance: `grep -rn "extern" src/ include/ | grep -v generated` returns 0 (all externs removed from headers)

**Files to modify**: All module pairs (`.h`/`.cpp`) that use extern globals

### P2.2 — Issue #9: Hardcoded Limits → Kconfig

**Current state**: `MAX_OUTPUTS=4` (`include/config_schema.h:6`), `MAX_SENDERS=16` (`src/core/sender_tracker.h:6`), `RDM_TOD_MAX=64` (`src/core/stats.h:44`), `MAX_SCENES=32` (`src/core/scene_engine.h:8`).

**Plan**:
1. Create `Kconfig` at project root with:
   ```
   config LUXDMX_MAX_OUTPUTS
       int "Maximum DMX outputs"
   default 4
   range 1 8
   ```
   Similar for `LUXDMX_MAX_SENDERS` (default 32), `LUXDMX_RDM_TOD_MAX` (default 32), `LUXDMX_MAX_SCENES` (default 32), `LUXDMX_LOG_BUF_CAP` (default 32)
2. Replace `#define` constants with `CONFIG_LUXDMX_*` macros in headers
3. Use dynamic allocation (`heap_caps_malloc` with `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`) for arrays sized by these limits
4. Build-flag fallback: `#ifndef CONFIG_LUXDMX_MAX_OUTPUTS` `#define MAX_OUTPUTS 4` for Arduino precompiled builds
5. Acceptance: `pio run -e esp32s3_psram` succeeds with `CONFIG_LUXDMX_MAX_OUTPUTS=8` (add build flag `-DCONFIG_LUXDMX_MAX_OUTPUTS=8` to verify)

**Files to modify**: `include/config_schema.h`, `src/core/sender_tracker.h`, `src/core/stats.h`, `src/core/scene_engine.h`, create `Kconfig`

### P2.3 — Issue #10: Unit Test Infrastructure

**Current state**: `test/native/` has `config_test.cpp`, `seqlock_test.cpp`, `merge_test.cpp`, `rdm_types_test.cpp` using a custom assert-based framework with shims. No Unity framework.

**Plan**:
1. Add Unity framework as a PlatformIO library dependency:
   - In `platformio.ini`: add `unity` to `[env:unit-test]` or use `lib_deps` for native env
   - Or vendor Unity into `test/unity/`
2. Create `test/unit/` directory with Unity-based tests:
   - `test/unit/test_merge_engine.cpp` — HTP/OFF/LTP/cross-universe
   - `test/unit/test_frame_router.cpp` — universe routing, split masking
   - `test/unit/test_scene_engine.cpp` — save/load, fade interpolation
   - `test/unit/test_config_schema.cpp` — field iteration, setValue/getValue
3. Add `[env:unit-test]` to `platformio.ini`:
   ```ini
   [env:unit-test]
   platform = native
   framework = 
   test_framework = unity
   ```
4. Keep existing `test/native/` tests as-is (parallel compatibility) — they test the shim layer
5. Migration goal: convert `test/native/` tests to Unity over time; for now, add new tests in Unity format
6. Acceptance: `pio test -e unit-test` passes; CI runs on every PR

**Files to create**: `Kconfig` (if needed for test env), `test/unit/*.cpp`, update `platformio.ini`

### P2.4 — Issue #11: Error Handling Consistency

**Current state**: Mix of `bool`, `int` (negative=error), `void`+global state across modules.

**Plan**:
1. Audit all public APIs for return type:
   - Functions returning `bool` → change to `esp_err_t` (return `ESP_OK` or `ESP_FAIL`/`ESP_ERR_*`)
   - Functions returning `int` (negative=error) → change to `esp_err_t` + out-parameter
   - `void` functions that set globals → keep `void` but ensure errors are logged with `ESP_LOGE`
2. Add `#include <esp_err.h>` and `#include <esp_log.h>` where missing
3. Replace `Serial.println`/`Serial.printf` error paths with `ESP_LOGE(TAG, ...)`
4. Ensure all public APIs in headers return `esp_err_t`
5. Acceptance: `grep -rn "printf\|println" src/ | wc -l` shows zero in error paths (only debug/info logs remain)

**Files to modify**: All `.cpp`/`.h` files with public APIs

## Phase 3: Medium Priority (Issues #12–19)

### P3.1 — Issue #12: Art-Net PollReply Completeness

**Current state**: `sendArtPollReply()` in `src/net/artnet_bridge.cpp:24-71` is missing: ESTA code (bytes 44-47), GoodInput (bytes 24-27), GoodOutput (bytes 28-31), SwIn, SwOut, SwVideo, SwMacro, SwRemote (bytes 32-42), Style (byte 43), MAC address (bytes 70-75), SubnetMask, Gateway, NodeReport (bytes 84-127).

**Plan**:
1. Populate ESTA code: use `0x4100` (registered ESTA) or `0x0000` (unclaimed) — `reply[44-47]`
2. Populate GoodInput/GoodOutput per port from live link state:
   - `GoodInput[i]`: set 0x01 (good input) per port
   - `GoodOutput[i]`: set 0x01 (good output) per port
3. Populate SwIn/SwOut from config port addresses (bytes 32-39)
4. Populate SwVideo/SwMacro/SwRemote (bytes 40-42) — all 0 for a 4-port DMX node
5. Populate Style (byte 43): `0x00` = unknown, `0x01` = DMX, `0x02` = RDM, `0xFF` = not supported
6. Populate MAC address (bytes 70-75) from `g_nodeMac`
7. Populate SubnetMask and Gateway from WiFi/Ethernet config (bytes 76-83)
8. Populate NodeReport (bytes 84-127) with status string: `"2000:0000:0001:0100"` (firmware version) + status
9. Acceptance: Art-Net controller (QLC+/Lightkey) shows all fields correctly

**Files to modify**: `src/net/artnet_bridge.cpp`

### P3.2 — Issue #13: sACN CID Persistence Race

**Current state**: `initCid()` in `src/net/sacn.cpp:36-53` reads/writes NVS without mutex. Called from `startSacn()` (core 0). If config import also touches CID concurrently, corruption can occur.

**Plan**:
1. Add a static mutex: `static SemaphoreHandle_t cidMutex`
2. Protect `initCid()` body with `xSemaphoreTake(cidMutex, portMAX_DELAY)` / `xSemaphoreGive(cidMutex)`
3. Load CID once at startup (before network tasks run) — `initCid()` already called from `startSacn()` which runs in `setup()` before `createTasks()`. Verify this ordering.
4. Add `cidMutex` initialization in `startSacn()` before first use
5. Acceptance: Concurrent boot + config import never corrupts CID

**Files to modify**: `src/net/sacn.cpp`, `src/net/sacn.h`

### P3.3 — Issue #14: mDNS TXT Records

**Current state**: `MDNS.addService()` calls exist in `src/main.cpp:80-82` but no TXT records are added.

**Plan**:
1. After `MDNS.begin()` and `MDNS.addService()`:
   - Art-Net TXT: `vers=4.2`, `port=6454`, `net=0`, `subnet=0` (from config per output)
   - sACN TXT: `cid=<hex>`, `universe=<1-based>`
2. Use `MDNS.addServiceTxt("artnet", "udp", "vers", "4.2")` syntax
3. For sACN, add `cid` and `universe` TXT records
4. Acceptance: `dns-sd -B _artnet._udp` and `dns-sd -B _e131._udp` show TXT records with correct fields

**Files to modify**: `src/main.cpp`

### P3.4 — Issue #15: WebSocket Protocol Documentation

**Current state**: No `docs/websocket-protocol.md` exists.

**Plan**:
1. Create `docs/websocket-protocol.md` documenting all WebSocket commands:
   - Binary frame layout: 16-byte header + 2048 DMX bytes + 20 output FPS + 10 nav tail
   - Text commands (JSON):
     - `{"cmd":"subscribe","universes":[0,1]}` — subscribe to universes (after P1.4)
     - `{"cmd":"scene","play":n,"fade":ms}` — play scene
     - RDM actions: `{"cmd":"rdm","action":"discover"}`, `{"cmd":"rdm","action":"setaddr","uid":"...","addr":n}`, `{"cmd":"rdm","action":"identify","uid":"..."}`, `{"cmd":"rdm","action":"setpers","uid":"...","pers":n}`, `{"cmd":"rdm","action":"setlabel","uid":"...","label":"..."}`
     - Config: `{"cmd":"config_get"}`, `{"cmd":"config_set","key":"...","value":"..."}`
     - OTA: `{"cmd":"ota_check"}`, `{"cmd":"ota_update"}`
     - Manual: `{"cmd":"manual",on:true}`, `{"cmd":"blackout"}`, `{"cmd":"viewout",n}`, `{"cmd":"identify",on:true}`
2. Document the binary frame header layout (field-by-field)
3. Acceptance: External client can implement full control from doc alone

**Files to create**: `docs/websocket-protocol.md`

### P3.5 — Issue #16: OTA Signature Verification

**Current state**: `OTA_SIGN_ENABLED=0` by default (`src/net/ota.cpp:14-16`). `src/net/ota_sign.cpp` exists with the verification framework.

**Plan**:
1. Change default: `#define OTA_SIGN_ENABLED 1` in `src/net/ota.cpp` (or add Kconfig option)
2. Add `OTA_SIGN_ENABLED` Kconfig option (default 1 for production)
3. In `platformio.ini`, add `-DOTA_SIGN_ENABLED=0` to dev environments (esp32s3dev, esp32s3_psram) for development
4. Document key provisioning flow:
   - Generate Ed25519 key pair: `python3 tools/gen_ota_keys.py`
   - Public key embedded in `ota_sign.h` via `-DOTA_SIGN_PUB_KEY="..."` build flag
   - Private key used by CI to sign release firmware: `python3 tools/sign_ota.py <firmware.bin> <private_key.pem> <output_signed.bin>`
5. Document key rotation: OTA endpoint accepts new public key in a separate signed manifest; fallback to unsigned for dev builds
6. Acceptance: Unsigned firmware rejected on `esp32s3_n16r8_eth`; signed firmware accepted; key rotation tested

**Files to modify**: `src/net/ota.cpp`, `platformio.ini`, create `tools/gen_ota_keys.py`, `tools/sign_ota.py`, `docs/ota-key-management.md`

### P3.6 — Issue #17: Rate Limiting on Config/OTA Endpoints

**Current state**: No rate limiting middleware on web server routes (`src/net/web_server.cpp`).

**Plan**:
1. Add IP-based rate limiter: `src/net/rate_limiter.{h,cpp}`
   - Token bucket per source IP: 5 req/min for `/ota/*`, 30 req/min for `/config/*`
   - Use a fixed-size hash map of IPs (max 32 entries) with `millis()`-based expiry (5 min)
   - Return HTTP 429 with `Retry-After` header
2. Add `rateLimitMiddleware()` in `web_server.cpp` that wraps OTA and config routes
3. Apply decorator pattern: `http.on("/ota/upload", HTTP_POST, rateLimitMiddleware(OTA_HANDLER, 5), ...)`
4. Acceptance: Burst requests return 429; legitimate use unaffected

**Files to create**: `src/net/rate_limiter.h`, `src/net/rate_limiter.cpp`
**Files to modify**: `src/net/web_server.cpp`, `src/net/web_routes.cpp`

### P3.7 — Issue #18: WiFi Credentials Unencrypted in NVS

**Current state**: `wifiPsk` stored plain in NVS (`templates/_base.ini:106` — `CFG_SECRET` flag masks in UI but storage is plain). `wifiSsid` stored plain too.

**Plan**:
1. Enable NVS encryption in `sdkconfig.defaults`:
   - `CONFIG_NVS_ENCRYPTION=y`
   - `CONFIG_NVS_ENCRYPT_YAML="nvs_keys/nvs_encrypted_keys.yaml"` (generate key file)
2. Generate NVS encryption key: `python3 tools/gen_nvs_key.py` → writes `nvs_keys/nvs_encrypted_keys.yaml`
3. Store `wifiPsk` and `wifiSsid` as encrypted blobs using `Preferences::putBytes()` with encryption flags
4. Update `net_state.cpp` `startWiFiStation()` to decrypt before use
5. Verify: `nvs_dump` shows encrypted `wifiPsk`; device boots with correct credentials
6. Note: Flash encryption (`CONFIG_ESP_FLASH_ENCRYPTION_MODE`) is a separate, more invasive step — document but don't enable by default (bricks if key lost)

**Files to modify**: `sdkconfig.defaults`, `src/net/net_state.cpp`, create `tools/gen_nvs_key.py`

### P3.8 — Issue #19: ArtIpProg Disabled by Default

**Current state**: `ipProg` config field defaults to `false` (neutral bool value). `handleArtIpProg()` is a minimal handler. The checklist says "enabled by default" but the code already defaults to disabled.

**Plan**:
1. Verify `ipProg` is `false` by default in all templates (confirmed: none set it)
2. Strengthen `handleArtIpProg()` to check `cfg.ipProg` before accepting:
   ```cpp
   static void handleArtIpProg(const uint8_t* p, int n, uint32_t ip) {
       if (!cfg.ipProg) return;  // silently drop if not enabled
       // Only accept from local subnet (169.254.x.x is OK for setup portal)
       if (!netIsLocalSubnet(ip)) return;
       // ... process IP/programming
   }
   ```
3. Acceptance: ArtIpProg packet from external IP ignored unless `ipProg` enabled + same subnet

**Files to modify**: `src/net/artnet_bridge.cpp`, `src/net/net_state.h`

## Phase 4: Low Priority (Issues #20–23)

### P4.1 — Issue #20: PlatformIO Platform Pin

**Current state**: `platformio.ini:13` uses zip URL:
```
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.39/platform-espressif32.zip
```

**Plan**:
1. Switch to registry version with checksum:
   ```ini
   platform = espressif32 @ ~55.03.39
   platform_packages =
       toolchain-esp32ulp @ 1.2.13
   ```
   Note: The pioarduino fork may not be on the main PlatformIO registry. If not available, use GitHub release with checksum verification via `platformio` manifest.
2. If registry version unavailable: use `platform = https://github.com/pioarduino/platform-espressif32.git#55.03.39` with SHA pin
3. Acceptance: `platformio.ini` uses registry version or pinned git ref, not zip URL

**Files to modify**: `platformio.ini`

### P4.2 — Issue #21: Firmware Size / Heap Regression Checks

**Current state**: No CI step for firmware size or heap monitoring.

**Plan**:
1. Add to `platformio.ini` a post-build script that parses `pio run -t size` output and checks thresholds:
   - DRAM: warn > 200KB, fail > 280KB
   - PSRAM: warn > 4MB, fail > 6MB
   - Flash: warn > 80%, fail > 95%
2. Add to CI workflow (`.github/workflows/ci.yml`):
   ```yaml
   - name: Check firmware size
     run: python3 tools/check_size.py --env esp32s3_n16r8_eth --dram-threshold-kb 200 --psram-threshold-mb 4
   ```
3. Add heap monitoring in soak test (`src/sys/soak_monitor.cpp` already logs DRAM/PSRAM)
4. Acceptance: PR fails if DRAM usage increases >5% without justification

**Files to create**: `tools/check_size.py`, `.github/workflows/ci.yml`

### P4.3 — Issue #22: SDKConfig Centralization

**Current state**: `CONFIG_ESP_BROWNOUT_DET=n`, `CONFIG_SPIRAM=y`, etc. repeated in each `[env:esp32s3*]` block via `custom_sdkconfig`. `sdkconfig.defaults` exists but is auto-generated by IDF.

**Plan**:
1. Create `sdkconfig.defaults` (or `sdkconfig.luxdmx_defaults`) with shared overrides:
   - `CONFIG_ESP_BROWNOUT_DET=n`
   - `CONFIG_ESP32S3_RTC_CLK_SRC_DEFAULT=1`
   - etc.
2. Create per-board overlays: `sdkconfig.esp32s3_psram` (SPIRAM), `sdkconfig.esp32s3_n16r8_eth` (SPIRAM + W5500), `sdkconfig.esp32dev` (no SPIRAM)
3. Remove per-env `custom_sdkconfig` blocks from `platformio.ini`, reference overlays instead
   - PlatformIO supports `board_custom_sdkconfig` or `build_flags` with `-D` for config overrides
4. Acceptance: `pio run -e esp32s3dev` picks up correct config; single source of truth

**Files to modify**: `sdkconfig.defaults`, create `sdkconfig.esp32s3_psram`, `sdkconfig.esp32s3_n16r8_eth`, `platformio.ini`

### P4.4 — Issue #23: 72h Soak Test Automation

**Current state**: `src/sys/soak_monitor.cpp` exists but no CI job. `LUXDMX_SOAK_TEST` flag only on `esp32s3_n16r8_eth` env.

**Plan**:
1. Create `.github/workflows/soak-test.yml`:
   - Scheduled job (weekly, trigger manually too)
   - Runs on `runs-on: ubuntu-latest` (or self-hosted for hardware access)
   - Flashes `esp32s3_n16r8_eth` build to hardware
   - Drives Art-Net 40fps × 4 outputs + sACN + WebSocket + RDM poll every 30s
   - `test/hardware/test_4output_load.py` extended to include soak monitoring
   - Monitors: DRAM/PSRAM, task watchdog resets, heap fragmentation, frame timing
   - Posts results as GitHub check
2. For hardware-less CI: run native host tests with simulated load for 1 hour (reduced)
3. Acceptance: Weekly soak test passes; results posted as GitHub check

**Files to create**: `.github/workflows/soak-test.yml`, modify `test/hardware/test_4output_load.py`

## Phase 5: Test Matrix (T01–T10)

| Test ID | Description | Plan Reference |
|---------|-------------|----------------|
| T01 | DMX Timing Accuracy — scope verify break=176µs, MAB=12µs, slot=4µs ±1% on all 4 outputs | P1.6, P1.1 |
| T02 | Art-Net/sACN Merge — 3 sources × 5 merge modes × 4 outputs = 60 combos | P2.4 |
| T03 | RDM Discovery — 1→32 responders; ToD completeness, UID uniqueness, no bus contention | P1.1 (dedicated RDM task) |
| T04 | ArtSync Staging — 10 sources → ArtSync → commit; <1 frame latency, no tearing | P3.8 (ArtIpProg) |
| T05 | sACN Sync — Sync universe + 4 data universes; commit on sync, 500ms grace, 2.5s timeout | P1.3 |
| T06 | Link Loss Policies — Pull Ethernet → verify each `linkLossMode` | P3.7 |
| T07 | OTA Rollback — Flash bad firmware → boot guard triggers factory reset after `OTA_BOOT_TRIES` | P3.5 |
| T08 | Soak Test — 72h continuous: Art-Net 40fps × 4 + sACN + WS + RDM poll | P4.4 |
| T09 | Config Import/Export — Round-trip JSON + XML; all fields preserved, secrets masked | P3.8 |
| T10 | WebSocket Load — 10 clients × 10 Hz push; no heap frag, no task watchdog reset | P1.4 |

## Phase 6: CLAUDE.md Firmware Evaluation Workflow Update

**Task**: Add a "Firmware Evaluation Workflow" section to `CLAUDE.md` documenting the hardware validation procedure for the ESP32-S3 N16R8 board.

**Current state**:
- WiFi credentials "MSI"/"12345678" already in `templates/esp32s3_psram.ini` (lines 6-7)
- Hostname "dmx-gateway" already in `templates/_base.ini` (line 9)
- MDNS already initialized in `src/main.cpp:79`
- Web server already started at `src/main.cpp:107`
- `dmx-gateway.local` already resolves via mDNS

**Plan**:
1. Add a new section "## Firmware Evaluation Workflow" after the existing "## Testing" section
2. Document the step-by-step procedure:

```markdown
## Firmware Evaluation Workflow

For validating firmware on ESP32-S3 N16R8 hardware before full project compile:

### Pre-flight: WiFi Default Configuration

The `esp32s3_psram` build target includes default WiFi credentials via
`templates/esp32s3_psram.ini`:
```ini
wifimode=0
wifissid=MSI
wifipsk=12345678
```

These defaults are applied at first boot when NVS is empty. They can be
overridden at runtime via the `/config` or `/setup` web endpoints.

### Step 1: Compile for esp32s3_psram

```bash
pio run -e esp32s3_psram
```

This compiles a minimal firmware with PSRAM enabled (8MB octal) but no
W5500 Ethernet — suitable for validating WiFi + DMX timing on the N16R8
hardware before building the full 4-universe Ethernet image.

### Step 2: Flash via COM7

```bash
pio run -e esp32s3_psram --target upload --upload-port COM7
```

> **S3 bootloader note**: Hold BOOT while plugging USB, or use
> `--before=no_reset` if auto-reset is unreliable.

### Step 3: Monitor Logs (5 minutes)

```bash
pio run -e esp32s3_psram --target monitor --upload-port COM7
```

Verify during 5-minute window:
- Serial output shows `[WiFi] joining 'MSI'` and successful DHCP
- `[ArtNet]` or `[sACN]` initialization messages appear
- No task watchdog resets or Guru Meditations
- `[RDM] task running on core 1` starts within 3 seconds of boot
- `free heap` stays above 200 KB

### Step 4: Verify Web Interface

Open `http://dmx-gateway.local` in a browser on the same network.
The mDNS hostname `dmx-gateway` is set in `templates/_base.ini` and
registered via `MDNS.begin()` in `src/main.cpp:79`.

If `dmx-gateway.local` does not resolve:
- Check that the ESP32 and browser are on the same network segment
- Try `ping dmx-gateway.local` — if that works, try the direct IP from the serial log
- Ensure the router/firewall allows mDNS (UDP 5353)

### Step 5: Proceed to Full Project

Only proceed to compile the full 4-universe project after successful
5-minute log validation and confirmed web interface access:

```bash
pio run -e esp32s3_n16r8_eth   # Full 4-universe build with W5500 Ethernet
```

The `esp32s3_n16r8_eth` environment adds:
- 4 DMX outputs (2 RDM-capable, 2 DMX-only)
- W5500 SPI Ethernet
- Soak test monitor (`LUXDMX_SOAK_TEST`)
```

3. Acceptance: CLAUDE.md includes the complete firmware evaluation workflow that another developer can follow to validate firmware on N16R8 hardware.

**Files to modify**: `CLAUDE.md`

## Execution Order & Dependencies

```
P1.6 → P1.1 → P1.3 (independent)
P1.2 (depends on P1.1 — needs RDM task to be stable)
P2.3 → P2.4 (tests validate P2.3 changes, P2.4 validates merge/frame_router)
P1.4 → T10
P1.5 → T09
P3.5 → T07
P4.4 → T08
Phase 6 (CLAUDE.md) is documentation-only, can run in parallel
```

## Risks

1. **Issue #9 (Kconfig)**: Switching from Arduino precompiled to IDF CMake build is non-trivial. The build_flags approach with `-D` overrides may be simpler than full Kconfig for a PlatformIO Arduino project.
2. **Issue #8 (Global state)**: Large refactor touching every file; high risk of breakage. Should be done module-by-module with tests passing between each.
3. **Issue #18 (NVS encryption)**: Requires generating encryption keys and storing them securely. If keys are lost, NVS data is unrecoverable. Enable only for production builds.
4. **Issue #5 (WS subscription)**: Changes the binary frame layout; frontend must be updated to match. Backward compatibility with non-subscribed clients must be maintained.
5. **Issue #10 (Unity tests)**: Existing native tests use a different framework; adding Unity creates dual test infrastructure. Plan keeps both for now.

## Deliverables

| Item | Type | Files Affected |
|------|------|----------------|
| P1.1 – P1.6 | Source code | 10+ files across src/net/, src/drv/, src/core/ |
| P2.1 – P2.4 | Source code + build | 15+ files, Kconfig, platformio.ini, test/unit/ |
| P3.1 – P3.8 | Source code + docs | 10+ files + docs/websocket-protocol.md, docs/ota-key-management.md |
| P4.1 – P4.4 | Build + CI + tools | platformio.ini, sdkconfig.*, .github/workflows/, tools/ |
| P5 | Tests | test/unit/, test/hardware/ |
| P6 | Documentation | CLAUDE.md |
| All issues marked ✅ in checklist | Documentation | FIRMWARE_EVALUATION_CHECKLIST.md |
