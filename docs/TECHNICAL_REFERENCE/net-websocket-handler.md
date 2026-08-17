# WebSocket Handler — Technical Reference

| Attribute | Value |
|---|---|
| **Layer** | `net` |
| **Module** | `ws_handler` |
| **Source Files** | `src/net/ws_handler.h`, `src/net/ws_handler.cpp` |
| **Protocol** | Binary frame envelope: see [WebSocket Protocol Reference](../websocket-protocol.md) |
| **Related** | [WebSocket Protocol](net-websocket-protocol.md), [WebSocket Frame Builder](net-ws-frame.md), [Web Server](net-web-server.md), [Web Routes](net-web-routes.md) |

## 1. File Inventory

| Source File | Purpose |
|---|---|
| `src/net/ws_handler.h` | `handleWsText`, `handleWsTextRdm`, `rdmWsProcessQueued` declarations |
| `src/net/ws_handler.cpp` | Browser→ESP32 WebSocket command dispatch and queued RDM execution |

## 2. Entry Points

| Symbol | Signature | Source |
|---|---|---|
| `handleWsText` | `void handleWsText(const char* payload, size_t len, uint32_t clientId)` | `src/net/ws_handler.cpp:140` |
| `handleWsTextRdm` | `void handleWsTextRdm(const String& msg)` | `src/net/ws_handler.cpp:41` |
| `rdmWsProcessQueued` | `void rdmWsProcessQueued()` | `src/net/ws_handler.cpp:93` |

## 3. Lifecycle

- `handleWsText` is called from `wsInit`'s `WS_EVT_DATA` event handler in `src/net/websocket.cpp:62`.
- The event handler is registered on the global `AsyncWebSocket ws("/ws")` at `src/net/websocket.cpp:48`.
- `wsInit(AsyncWebServer& srv)` is called from `main.cpp:126` during `setup()` phase 8 (Web server + WebSocket).
- `rdmWsProcessQueued()` is called from `main.cpp:150` inside `loop()` — after the DMX task has released the RMT channel.

## 4. Text Frame Dispatch

```
WebSocket text frame arrives
    └─ src/net/websocket.cpp:62 (WS_EVT_DATA)
        └─ handleWsText(payload, len, clientId)   src/net/ws_handler.cpp:140
            ├─ "subscribe"  → wsClientSub[slot]   src/net/ws_handler.cpp:147
            ├─ "viewout"   → monitorOut           src/net/ws_handler.cpp:171
            ├─ "blackout"  → dmxBufWriteBegin      src/net/ws_handler.cpp:179
            ├─ "mode"      → stats().manualMode    src/net/ws_handler.cpp:184
            ├─ "identify"  → identifyCh/identifyUntil src/net/ws_handler.cpp:188
            ├─ "set"       → dmxBufWriteEndSet     src/net/ws_handler.cpp:197
            ├─ "scene"/"play" → sceneTriggerPlay   src/net/ws_handler.cpp:209
            ├─ "saveScene" → dmxBufSnapshot         src/net/ws_handler.cpp:220
            ├─ "clearScene"→ sceneEraseNvs          src/net/ws_handler.cpp:242
            └─ handleWsTextRdm(msg)                 src/net/ws_handler.cpp:249
```

## 5. Command Handlers (Browser→ESP32)

### 5.1 Subscribe
```json
{"subscribe": true, "universes": [0, 1]}
```
- Parses the `universes` array and computes a bitmask.
- Stored in `wsClientSub[clientId % WS_MAX_CLIENTS]` (`src/net/ws_handler.cpp:166`).
- Default on connect: `0x000F` (all 4 universes) at `src/net/websocket.cpp:54`.
- Subscription is checked in `wsPush()` at `src/net/websocket.cpp:24`: `wsClientSub[i]`.

### 5.2 View Output
```json
{"viewout": 0, "out": 1}
```
- Sets `monitorOut` if the output is enabled (`src/net/ws_handler.cpp:175`).
- `monitorOut` is consumed by `viewOutput()` at `src/core/output_init.cpp:23` to determine which output's DMX the browser receives.

### 5.3 Blackout
```json
{"blackout": true}
```
- Zeroes 512 channels in-place via the seqlock write begin/end pattern at `src/net/ws_handler.cpp:180-182`.

### 5.4 Manual Mode
```json
{"mode": "manual", "enabled": true}
```
- Sets `stats().manualMode` (`src/net/ws_handler.cpp:185`).
- Checked in the merge engine at `src/core/merge_engine.cpp:47` to bypass network sources.

### 5.5 Identify (Channel)
```json
{"identify": true, "ch": 1}
```
- Sets `identifyCh` and `identifyUntil = millis() + IDENTIFY_MS` at `src/net/ws_handler.cpp:193-194`.
- These externs are declared at `src/net/ws_handler.cpp:14-15`.

### 5.6 Set Channel
```json
{"set": {"ch": 1, "val": 203}}
```
- Writes into the seqlock-protected DMX buffer via `dmxBufWriteBegin`/`dmxBufWriteEndSet` at `src/net/ws_handler.cpp:205-206`.

### 5.7 Scene Play / Save / Clear
```json
{"scene": {"play": 0, "fade": 500}}
{"saveScene": {"idx": 0, "name": "Work"}}
{"clearScene": {"idx": 0}}
```
- Play: `sceneTriggerPlay(sceneIdx, fadeMs)` at `src/net/ws_handler.cpp:216`.
- Save: `dmxBufSnapshot` + `sceneSaveNvs` at `src/net/ws_handler.cpp:227,238`.
- Clear: `sceneEraseNvs` at `src/net/ws_handler.cpp:246`.

## 6. RDM Command Handling

`handleWsTextRdm` parses 5 RDM action types from the JSON string (`src/net/ws_handler.cpp:41`):

| Action | Fields Extracted | Pending State |
|---|---|---|
| `"discover"` | — | `g_pendingAction = 1` (`src/net/ws_handler.cpp:45`) |
| `"setaddr"` | `uid`, `addr` | `g_pendingAction = 2`, `g_pendingUid`, `g_pendingAddr` (`src/net/ws_handler.cpp:55`) |
| `"identify"` | `uid` | `g_pendingAction = 3`, `g_pendingUid` (`src/net/ws_handler.cpp:62`) |
| `"setpers"` | `uid`, `pers` | `g_pendingAction = 4` (`src/net/ws_handler.cpp:72`) |
| `"setlabel"` | `uid`, `label` | `g_pendingAction = 5` (`src/net/ws_handler.cpp:86`) |

- UID parsing: hex string `"AABBCCDDEEFF"` → `rdm_uid_t` using `sscanf` at `src/net/ws_handler.cpp:30-35`.
- All RDM operations are deferred: they set `g_pendingAction` and are executed later by `rdmWsProcessQueued()` (`src/net/ws_handler.cpp:93`).

## 7. Queued RDM Execution

`rdmWsProcessQueued()` runs from `loop()` at `src/main.cpp:150`:

| Action | Function Called | Source |
|---|---|---|
| 1 (discover) | `rdmRmtSelect(rdmOut)`, `rdmRmtDiscover()` | `src/net/ws_handler.cpp:102` |
| 2 (setaddr) | `rdmOpSetAddr()` | `src/net/ws_handler.cpp:117` |
| 3 (identify) | `rdmOpSetIdentify()` | `src/net/ws_handler.cpp:123` |
| 4 (setpers) | `rdmOpSetPersonality()` | `src/net/ws_handler.cpp:128` |
| 5 (setlabel) | `rdmOpSetString(RDM_PID_DEVICE_LABEL)` | `src/net/ws_handler.cpp:133` |

- Discovery populates `stats().rdmTod[0..count-1]` and sets `stats().rdmCount` (`src/net/ws_handler.cpp:105-108`).
- `rdmPollDirty` is set to `true` after discovery and setaddr, triggering `/rdm.json` regeneration (`src/net/ws_handler.cpp:112,118`).
- No blocking: `g_pendingAction` is reset to 0 before execution (`src/net/ws_handler.cpp:96-97`).

## 8. Static State

| Variable | Scope | Source |
|---|---|---|
| `g_pendingUid` | `static rdm_uid_t` | `src/net/ws_handler.cpp:17` |
| `g_pendingAction` | `static int` | `src/net/ws_handler.cpp:18` |
| `g_pendingAddr` | `static uint16_t` | `src/net/ws_handler.cpp:19` |
| `g_pendingPers` | `static uint8_t` | `src/net/ws_handler.cpp:19` |
| `g_pendingLabel` | `static char[33]` | `src/net/ws_handler.cpp:21` |

- All are file-local to `ws_handler.cpp`; no global header exposure.
- `identifyCh` and `identifyUntil` are `extern` in `ws_handler.cpp:14-15`, defined elsewhere.

## 9. Cross-Core Interaction

- `handleWsText` executes on **core 0** (AsyncWebServer task, `platformio.ini:42`).
- It writes to the seqlock-protected DMX buffer (`dmxBufWriteBegin/EndSet` at `src/net/ws_handler.cpp:180,205`) — readable by core 1's `dmxTxTask`.
- `rdmRmtSelect`, `rdmRmtDiscover`, `rdmOpSetAddr` all execute on core 0 via `rdmWsProcessQueued` at `src/main.cpp:150`.
- The RDM task engine (`rdm_task.cpp`) normally runs on core 1 (`src/sys/tasks.cpp:83`, priority 19); the deferred path here is a synchronous fallback that runs after `dmxTxTask`'s 1 ms tick releases the RMT channel.

## 10. Configuration Integration

- `cfg.outputs[o].enabled` is checked before setting `monitorOut` (`src/net/ws_handler.cpp:175`).
- `cfg.outputs[i].txStyle` (checked in merge engine at `src/core/merge_engine.cpp:47`) determines whether manual or continuous output mode is active.

## 11. Data Structures

### 11.1 RDM UID Parsing (`src/net/ws_handler.cpp:23`)
```cpp
static bool parseUid(const String& msg, rdm_uid_t& uid);
```
- Extracts `"uid":"AABBCCDDEEFF"` via `indexOf`/`substring`.
- `man_id` = first 4 hex chars; `dev_id` = last 8 hex chars (`src/net/ws_handler.cpp:32-33`).

## 12. Error Handling

| Condition | Source | Behavior |
|---|---|---|
| UID parse fail | `src/net/ws_handler.cpp:50,60,77` | Silent return — no error response sent |
| Invalid output index | `src/net/ws_handler.cpp:175` | `o >= 0 && o < MAX_OUTPUTS && cfg.outputs[o].enabled` guard |
| Invalid channel | `src/net/ws_handler.cpp:192,203` | `ch < 1 \|\| ch > 512` → early return |
| Scene index out of range | `src/net/ws_handler.cpp:224` | `sceneIdx < 0 \|\| sceneIdx >= MAX_SCENES` → early return |
| Missing scene data | `src/net/ws_handler.cpp:225` | `if (!g_scenes) return;` |

## 13. Timing Constraints

| Operation | Constraint | Source |
|---|---|---|
| `rdmWsProcessQueued` | Runs in `loop()` every iteration (core 0) | `src/main.cpp:150` |
| `handleWsText` | Must return quickly — called from AsyncWebServer event callback | `src/net/ws_handler.cpp:140` |
| Channel set | Seqlock write (`dmxBufWriteBegin/EndSet`) | `src/net/ws_handler.cpp:205-206` |
| Blackout | Seqlock write (zero-fill) | `src/net/ws_handler.cpp:180-182` |

## 14. Memory Model

- `ws_handler.cpp` statics: ~64 bytes for pending action state.
- `String msg(payload, len)` allocates on the heap per incoming frame (`src/net/ws_handler.cpp:141`).
- `g_scenes` is `extern` — defined in `scene_engine` (`src/core/scene_engine.cpp`).
- No dynamic allocation in `handleWsTextRdm` or `rdmWsProcessQueued`.

## 15. Logging

| Level | Source | Content |
|---|---|---|
| INFO | `src/net/ws_handler.cpp:167` | Client subscription bitmap |
| INFO | `src/net/ws_handler.cpp:110` | RDM discovery results (UID print) |
| INFO | `src/net/ws_handler.cpp:217` | Scene play trigger |
| INFO | `src/net/ws_handler.cpp:239` | Scene save |

## 16. Performance

- `handleWsText` uses `String::indexOf` (linear scan) for each command — O(n) per token, acceptable given `<1024` byte payloads.
- Subscribe parsing loops character-by-character through the `universes` array (`src/net/ws_handler.cpp:153-164`).
- `rdmWsProcessQueued` is a no-op when `g_pendingAction == 0` (`src/net/ws_handler.cpp:94`).

## 17. Security

- All text frames are handled synchronously on core 0 with no authentication (LAN-only per `src/net/web_server.cpp:58`).
- Rate limiting for HTTP endpoints is upstream in [Web Server](net-web-server.md) via [Rate Limiter](net-rate-limiter.md).
- WebSocket frames themselves are not rate-limited — controlled by the browser's send frequency.

## 18. Integration with Other Modules

| Module | Integration Point | Source |
|---|---|---|
| [WebSocket Frame Builder](net-ws-frame.md) | `ws_clientSub[]` subscription check in `wsPush()` | `src/net/websocket.cpp:24` |
| [Stats](core-stats.md) | `stats().manualMode`, `stats().rdmCount`, `stats().rdmTod` | `src/net/ws_handler.cpp:105,107` |
| Scene Engine | `sceneTriggerPlay`, `sceneSaveNvs`, `sceneEraseNvs`, `g_scenes` | `src/net/ws_handler.cpp:216,238,246,225` |
| RDM Engine | `rdmRmtSelect`, `rdmRmtDiscover`, `rdmOpSetAddr`, etc. | `src/net/ws_handler.cpp:102-133` |
| Output Init | `viewOutput()`, `monitorOut` | `src/net/ws_handler.cpp:180,204,226` |
| DMX Buffer | `dmxBufWriteBegin/End`, `dmxBufWriteEndSet`, `dmxBufSnapshot` | `src/net/ws_handler.cpp:181,206,227` |
| Config Core | `saveConfig()` (via `cfgcore::save`) | `src/cfg/config_core.h:26` |

## 19. Testing

- No host-native tests for `ws_handler` directly.
- Playwright E2E tests exercise the WebSocket command path via the running browser (`docs/e2e/` — not yet in the repo).

## 20. Known Limitations

- `String`-based JSON parsing (no `ArduinoJson` deserialization) — vulnerable to substring false-positives.
- RDM operations are synchronous on core 0 — long discovery (8 s budget, `src/core/rdm_disc.cpp`) blocks `loop()` and WebSocket pushes.

## 21. References

- WebSocket binary frame layout: [`docs/websocket-protocol.md`](../websocket-protocol.md)
- wsInit registration: [`src/main.cpp:126`](src/main.cpp#L126)
- wsHandler loop call: [`src/main.cpp:150`](src/main.cpp#L150)
- WebSocket server instance: [`src/net/websocket.cpp:8`](src/net/websocket.cpp#L8)
- Frame push logic: [`src/net/websocket.cpp:14`](src/net/websocket.cpp#L14)
- Subscription default: [`src/net/websocket.cpp:54`](src/net/websocket.cpp#L54)
- WS_MAX_CLIENTS constant: [`src/net/ws_frame.h:14`](src/net/ws_frame.h#L14)
- Stats state: [`src/core/stats.h:6`](src/core/stats.h#L6)
- RDM UID type: [`include/rdm_types.h`](include/rdm_types.h)
- Config fields: [`include/config_schema.h`](include/config_schema.h)
- AsyncTCP core pinning: [`platformio.ini:42`](platformio.ini#L42)
