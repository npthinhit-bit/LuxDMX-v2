# WebSocket Protocol — Technical Reference

| Attribute | Value |
|---|---|
| **Layer** | `net` |
| **Module** | `websocket` |
| **Source Files** | `src/net/websocket.h`, `src/net/websocket.cpp` |
| **Protocol Constants** | `src/net/ws_frame.h` |
| **Related** | [WebSocket Handler](net-ws-handler.md), [WebSocket Frame Builder](net-ws-frame.md), [Web Server](net-web-server.md) |

## 1. File Inventory

| Source File | Purpose |
|---|---|
| `src/net/websocket.h` | Global `AsyncWebSocket ws("/ws")` declaration, `wsPush`, `wsPushMeta`, `wsInit`, `handleWsText` |
| `src/net/websocket.cpp` | WebSocket server instance, client tracking, `wsPush`, `wsPushMeta`, `wsInit` |
| `src/net/ws_frame.h` | Frame layout constants (`WS_HEADER_LEN`, `WS_CHANS_ALL`, `WS_FRAME_LEN`, etc.), `wsBuf[]`, `wsBuildFrame()` |
| `src/net/ws_frame.cpp` | `wsBuildFrame()` implementation — populates `wsBuf` from current state |

## 2. WebSocket Server Instance

```cpp
AsyncWebSocket ws("/ws");
```
- Declared in `src/net/websocket.cpp:8`.
- Exposed as `extern AsyncWebSocket ws` in `src/net/websocket.h:7`.
- Registered on the HTTP server via `wsInit(http)` called from `src/main.cpp:126`.
- Mount path: `/ws` (matches browser `new WebSocket('ws://' + location.host + '/ws')` at `src/frontend/scripts/index_js.h:228` and `src/frontend/scripts/rdm_js.h:14`).

## 3. Lifecycle

### 3.1 Initialization
```
main.cpp:126 → wsInit(http)
    → src/net/websocket.cpp:47
        → ws.onEvent(...) registers WS_EVT_CONNECT / WS_EVT_DISCONNECT / WS_EVT_DATA callbacks
        → srv.addHandler(&ws)
```

### 3.2 Event Callbacks (registered in `wsInit`)
| Event | Handler | Source Line |
|---|---|---|
| `WS_EVT_CONNECT` | Assigns client to slot, default subscription `0x000F`, zeroes frame seq | `src/net/websocket.cpp:52` |
| `WS_EVT_DISCONNECT` | Clears `wsClients[slot]`, zeroes subscription | `src/net/websocket.cpp:57` |
| `WS_EVT_DATA` | Calls `handleWsText((const char*)data, len, id)` | `src/net/websocket.cpp:62` |

## 4. Client Tracking

```cpp
static AsyncWebSocketClient* wsClients[WS_MAX_CLIENTS];  // src/net/websocket.cpp:11
static uint32_t wsClientFrameSeq[WS_MAX_CLIENTS];         // src/net/websocket.cpp:12
```

| Concept | Detail |
|---|---|
| Max clients | `WS_MAX_CLIENTS = 12` (`src/net/ws_frame.h:14`) |
| Slot assignment | `clientId % WS_MAX_CLIENTS` (`src/net/websocket.cpp:51`) |
| Subscription mask | `wsClientSub[slot]` (`extern` at `src/net/ws_frame.h:29`), 4-bit (one per universe) |
| Default subscription | `0x000F` — all 4 universes (`src/net/websocket.cpp:54`) |
| Frame seq tracking | `wsClientFrameSeq[slot]` — used for delta detection in `wsPush()` (`src/net/websocket.cpp:26`) |

## 5. Binary Frame Layout

The binary frame is built by `wsBuildFrame()` into the static `wsBuf[WS_FRAME_LEN]` buffer (`src/net/ws_frame.h:18`, defined at `src/net/ws_frame.cpp:12`).

| Offset | Length | Field | Source |
|---|---|---|---|
| 0 | 2 | Global FPS (fixed-point ×10) | `src/net/ws_frame.cpp:35` |
| 2 | 2 | RSSI / link speed (signed int16) | `src/net/ws_frame.cpp:36` |
| 4 | 4 | Free heap (uint32) | `src/net/ws_frame.cpp:37-38` |
| 8 | 4 | Uptime seconds (uint32) | `src/net/ws_frame.cpp:39-40` |
| 12 | 1 | Active sender count | `src/net/ws_frame.cpp:41` |
| 13 | 1 | Source status (0=normal, 1=conflict, 2=merging) | `src/net/ws_frame.cpp:42` |
| 14 | 2 | Jitter (fixed-point ×10) | `src/net/ws_frame.cpp:43` |
| 16 | 2048 | DMX data: 512 channels × 4 outputs | `src/net/ws_frame.cpp:55` |
| 2064 | 10 | Per-output stats: out fps(2) + in fps(2) + tx style(1) ×4 | `src/net/ws_frame.cpp:65-85` |
| 2074 | 1 | Changed-universe bitmap | `src/net/ws_frame.cpp:59` |
| 2075 | 10 | Navigation tail: fixtures(2) + rdmTx(4) + rdmRx(4) | `src/net/ws_frame.cpp:88-95` |

**Total frame length: 2095 bytes** (`WS_FRAME_LEN = WS_CHANGED_OFF + 1 + WS_NAV_TAIL = 2085 + 1 + 10 = 2096`... actual constant is `src/net/ws_frame.h:13`).

### 5.1 Constants (`src/net/ws_frame.h`)
```cpp
WS_HEADER_LEN   = 16         // src/net/ws_frame.h:6
WS_CHANS_PER_OUT = 512       // src/net/ws_frame.h:7
WS_CHANS_ALL    = 2048       // src/net/ws_frame.h:8
WS_PER_OUT      = 5          // src/net/ws_frame.h:9
WS_PEROUT_ALL   = 20         // src/net/ws_frame.h:10
WS_NAV_TAIL     = 10         // src/net/ws_frame.h:11
WS_CHANGED_OFF  = 2084       // src/net/ws_frame.h:12
WS_FRAME_LEN    = 2095       // src/net/ws_frame.h:13
WS_MAX_CLIENTS  = 12        // src/net/ws_frame.h:14
```

### 5.2 Header Fields

| Byte Offset | Field | Type | Notes |
|---|---|---|---|
| 0-1 | `fps` | uint16 | `stats().fps * 10.0f` (`src/net/ws_frame.cpp:26`) |
| 2-3 | `rssi` | int16 | WiFi RSSI, or link speed on Ethernet (`src/net/ws_frame.cpp:27-30`) |
| 4-7 | `heap` | uint32 | `ESP.getFreeHeap()` (`src/net/ws_frame.cpp:31`) |
| 8-11 | `uptime_s` | uint32 | `uptimeSec()` (`src/net/ws_frame.cpp:32`) |
| 12 | `senders` | uint8 | `activeSenderCount()` (`src/net/ws_frame.cpp:41`) |
| 13 | `srcStatus` | uint8 | `stats().srcStatus` — 0=normal, 1=conflict, 2=merging (`src/net/ws_frame.cpp:42`) |
| 14-15 | `jitter` | uint16 | `stats().jitterMs * 10.0f` (`src/net/ws_frame.cpp:33`) |

### 5.3 DMX Data Block (offset 16)

- 4 outputs × 512 channels = 2048 bytes.
- Each output's 512 channels are at `wsBuf[WS_HEADER_LEN + i * WS_CHANS_PER_OUT]` (`src/net/ws_frame.cpp:55`).
- Only channels 1-512 are compared (start code at `data[0]` is skipped) (`src/net/ws_frame.cpp:48`).
- Delta detection: `wsLastDmx[i][j]` comparison at `src/net/ws_frame.cpp:51`.

### 5.4 Per-Output Stats (offset 2064 = `WS_HEADER_LEN + WS_CHANS_ALL`)

| Byte Offset | Length | Field | Source |
|---|---|---|---|
| 2064 + 0 | 2 | Output 0 TX FPS | `src/net/ws_frame.cpp:65` |
| 2064 + 2 | 2 | Output 1 TX FPS | |
| 2064 + 4 | 2 | Output 2 TX FPS | |
| 2064 + 6 | 2 | Output 3 TX FPS | |
| 2064 + 8 | 2 | Output 0 RX FPS | `src/net/ws_frame.cpp:75` |
| 2064 + 10 | 2 | Output 1 RX FPS | |
| 2064 + 12 | 2 | Output 2 RX FPS | |
| 2064 + 14 | 2 | Output 3 RX FPS | |
| 2064 + 16 | 1 | Output 0 TX style | `src/net/ws_frame.cpp:83` |
| 2064 + 17 | 1 | Output 1 TX style | |
| 2064 + 18 | 1 | Output 2 TX style | |
| 2064 + 19 | 1 | Output 3 TX style | |

**TX style byte** (bitmask at `src/net/ws_frame.cpp:82-83`):
- Bit 0: `dmxIsDelta(i)` — delta mode follows the input
- Bit 1: `TXSRC_ARTNET` — transmit style set over Art-Net

### 5.5 Changed-Universe Bitmap (offset 2074 = `WS_CHANGED_OFF`)
```cpp
wsBuf[WS_CHANGED_OFF] = wsChangedBitmap;  // src/net/ws_frame.cpp:59
```
- Bit *i* set if universe *i*'s DMX data changed since the last `wsBuildFrame()` call.
- Used in `wsPush()` for delta-only delivery: `wsClientSub[i]` AND `wsChangedBitmap` (`src/net/websocket.cpp:24`).

### 5.6 Navigation Tail (offset 2075, 10 bytes)

| Offset | Length | Field | Source |
|---|---|---|---|
| 2075 | 2 | `rdmCount` (fixture count) | `src/net/ws_frame.cpp:91` |
| 2077 | 4 | `rdmSent` (cumulative) | `src/net/ws_frame.cpp:92-93` |
| 2081 | 4 | `rdmRecv` (cumulative) | `src/net/ws_frame.cpp:94-95` |

## 6. Frame Push Logic (`wsPush`)

Source: `src/net/websocket.cpp:14`

```
wsPush():
    if ws.count() == 0: return                    // src/net/websocket.cpp:15
    wsBuildFrame()                                // src/net/websocket.cpp:16
    for each slot 0..WS_MAX_CLIENTS-1:            // src/net/websocket.cpp:19
        if !wsClients[i]: continue                // src/net/websocket.cpp:21
        if status != WS_CONNECTED: clear + continue  // src/net/websocket.cpp:22
        changed = wsChangedBitmap & wsClientSub[i]    // src/net/websocket.cpp:24
        if changed == 0 && wsClientFrameSeq[i] == wsFrameSeq: continue  // src/net/websocket.cpp:26
        wsClientFrameSeq[i] = wsFrameSeq               // src/net/websocket.cpp:27
        if !c->canSend(): continue                      // src/net/websocket.cpp:28
        c->binary(wsBuf, WS_FRAME_LEN)                 // src/net/websocket.cpp:29
```

- Called from `src/main.cpp:159` in `loop()` at ~100 ms intervals (10 Hz).
- `wsFrameSeq` is incremented once per `wsBuildFrame()` call (`src/net/ws_frame.cpp:22`).

## 7. Meta Push Logic (`wsPushMeta`)

Source: `src/net/websocket.cpp:33`

```
wsPushMeta():
    if ws.count() == 0: return                    // src/net/websocket.cpp:35
    if !ws.availableForWriteAll(): return        // src/net/websocket.cpp:35
    if freeHeap < 40000: return                   // src/net/websocket.cpp:36
    if maxAllocHeap < 24000: return               // src/net/websocket.cpp:36
    text: {"meta":1,"senders":<sendersJson()>,"log":<logJson()>}  // src/net/websocket.cpp:38-42
    ws.textAll(m)                                // src/net/websocket.cpp:43
```

- Called from `src/main.cpp:163` in `loop()` at ~2000 ms intervals (2 Hz).
- `sendersJson()` defined in `src/net/web_routes.cpp:532`.
- `logJson()` defined in `src/net/web_routes.cpp:551` (currently returns `"[]"` — empty log stub at `src/net/web_routes.cpp:551-558`).

## 8. Text Frame Dispatch

Text frames are dispatched to `handleWsText` via the `WS_EVT_DATA` callback (`src/net/websocket.cpp:62`):

```cpp
if (type == WS_EVT_DATA) {
    if (len > 0 && data) handleWsText((const char*)data, len, id);
}
```

- Full command reference: see [WebSocket Handler](net-ws-handler.md).
- Commands include: `subscribe`, `viewout`, `blackout`, `mode`, `identify`, `set`, `scene`, `saveScene`, `clearScene`, and RDM actions.

## 9. Client Subscription Filtering

- Browser sends `{"subscribe":true,"universes":[0,1]}` → handler parses to bitmask.
- Stored in `wsClientSub[clientId % WS_MAX_CLIENTS]` (`src/net/ws_handler.cpp:166`).
- `wsPush()` checks `wsChangedBitmap & wsClientSub[i]` (`src/net/websocket.cpp:24`).
- Unsubscribed changed universes are skipped — reduces CPU and bandwidth (`src/net/ws_handler.cpp:145`).

## 10. Memory Model

| Buffer | Size | Location | Purpose |
|---|---|---|---|
| `wsBuf` | `WS_FRAME_LEN` (2095) | `src/net/ws_frame.cpp:12` | Single reusable frame buffer — no per-frame allocation |
| `wsClients` | `WS_MAX_CLIENTS` (12) pointers | `src/net/websocket.cpp:11` | Client pointer tracking |
| `wsClientFrameSeq` | 12 × uint32 | `src/net/websocket.cpp:12` | Per-client frame sequence for delta detection |
| `wsLastDmx` | 4 × 512 bytes | `src/net/ws_frame.cpp:19` | Last-sent DMX for delta comparison |
| `wsClientSub` | 12 × uint16 | `src/net/ws_frame.h:29` (extern) | Per-client universe subscription bitmask |

## 11. Configuration Integration

- WebSocket enable is gated by route registration in `src/net/web_server.cpp` (see [Web Server](net-web-server.md)).
- `wsPush` and `wsPushMeta` are always called in `loop()` regardless of client count — the `ws.count() == 0` check makes them no-ops (`src/net/websocket.cpp:15,35`).

## 12. Cross-Core Interaction

```
Core 0 (loop):                      Core 1 (dmxTxTask, prio 19):
  wsPush()                          snapshotAndTransmit()
    └─ wsBuildFrame()                └─ dmxBufSnapshot()  (seqlock read)
       └─ reads dmxBufferState()     └─ rmtDmxKick()
       └─ reads stats()              └─ stats() writes
    └─ c->binary(wsBuf)   ──TCP──>   [DMX line]
  wsPushMeta()
    └─ sends text frame
```

- `wsBuildFrame` reads from `dmxBufferState().buffers[i].data[1]` via seqlock-protected snapshot (`src/net/ws_frame.cpp:48`).
- `dmxTxTask` writes the buffer on core 1 (`src/sys/tasks.cpp:138`).
- The seqlock guarantees no torn reads (`include/seqlock.h:9`).

## 13. Performance Constraints

| Metric | Value | Source |
|---|---|---|
| Frame push rate | 10 Hz (100 ms) | `src/main.cpp:158` |
| Meta push rate | 2 Hz (2000 ms) | `src/main.cpp:162` |
| Frame size | 2095 bytes | `src/net/ws_frame.h:13` |
| Max concurrent clients | 12 | `src/net/ws_frame.h:14` |
| Heap guard for meta push | 40 KB min free, 24 KB min max-alloc | `src/net/websocket.cpp:36` |
| `canSend()` check | Per client | `src/net/websocket.cpp:28` |

## 14. Error Handling

| Error | Source | Behavior |
|---|---|---|
| No clients connected | `src/net/websocket.cpp:15,35` | Early return (no-op) |
| Client disconnected mid-iteration | `src/net/websocket.cpp:57-59` | Slot cleared, subscription zeroed |
| Client not writable | `src/net/websocket.cpp:28` | Skip (don't block) |
| Frame seq already seen by client | `src/net/websocket.cpp:26` | Skip (delta-only delivery) |
| Binary send exception | `src/net/websocket.cpp:29` | Caught with `try/catch` (`src/net/websocket.cpp:29`) |

## 15. Logging

| Level | Source | Content |
|---|---|---|
| INFO | `src/net/websocket.cpp:56` | Client connect: ID, slot, subscription |
| INFO | `src/net/websocket.cpp:60` | Client disconnect: ID, slot |

## 16. Security

- WebSocket requires no authentication — LAN-only assumption (`src/net/web_server.cpp:58`).
- Binary frame is pushed only to clients matching `ws.count() > 0` check.
- Text frame parsing is delegated to `handleWsText` which uses `String`-based substring matching (see [Handler](net-ws-handler.md)).
- Heap guard prevents OOM-driven crashes during meta push (`src/net/websocket.cpp:36`).

## 17. Testing

- `ws_frame` constants are compile-time computed; no runtime test needed.
- Playwright E2E tests verify binary frame decoding in the browser (`docs/e2e/` — not yet in repo).
- Frontend `LuxNav.stats(e.data)` decodes the frame client-side (`src/frontend/base/navbar.h:51`).

## 18. Known Issues & Considerations

- `logJson()` at `src/net/web_routes.cpp:551` returns `"[]"` — the log buffer (`stats().logBuf`) is populated by `maybeLog()` but never serialized to JSON.
- The `try/catch` around `c->binary()` (`src/net/websocket.cpp:29`) catches `std::exception` but AsyncTCP may throw non-standard exceptions on severe network errors.

## 19. Integration Matrix

| Consumer | Uses |
|---|---|
| [WebSocket Handler](net-ws-handler.md) | `handleWsText` dispatch, `wsClientSub` |
| [Web Server](net-web-server.md) | `wsInit(http)` registration |
| [Main Loop](src/main.cpp:133) | `wsPush()` at 10 Hz, `wsPushMeta()` at 2 Hz |
| [Frame Builder](net-ws-frame.md) | `wsBuf`, `wsChangedBitmap`, `wsFrameSeq` |

## 20. Related Environment Configuration

- AsyncTCP pinned to core 0 (`platformio.ini:42`): `-DCONFIG_ASYNC_TCP_RUNNING_CORE=0`.
- AsyncTCP stack size: 16 KB (`platformio.ini:40`).
- AsyncTCP queue depth: 128 (`platformio.ini:41`).
- AsyncTCP priority: 10 (`platformio.ini:42`).

## 21. References

- Handler commands: [WebSocket Handler](net-ws-handler.md)
- Frame builder: [WebSocket Frame Builder](net-ws-frame.md)
- Web server routes: [Web Server](net-web-server.md)
- Seqlock buffer: [`include/seqlock.h`](../../../include/seqlock.h)
- Main loop push calls: [`src/main.cpp:159`](src/main.cpp#L159)
- `wsInit` call in setup: [`src/main.cpp:126`](src/main.cpp#L126)
- Frame push no-op guard: [`src/net/websocket.cpp:15`](src/net/websocket.cpp#L15)
- Meta heap guard: [`src/net/websocket.cpp:36`](src/net/websocket.cpp#L36)
- Frame length constant: [`src/net/ws_frame.h:13`](src/net/ws_frame.h#L13)
- Max clients: [`src/net/ws_frame.h:14`](src/net/ws_frame.h#L14)
- Client slot assignment: [`src/net/websocket.cpp:51`](src/net/websocket.cpp#L51)
- Navbar stats decoder: [`src/frontend/base/navbar.h:51`](src/frontend/base/navbar.h#L51)
- Index page socket connect: [`src/frontend/scripts/index_js.h:228`](src/frontend/scripts/index_js.h#L228)
