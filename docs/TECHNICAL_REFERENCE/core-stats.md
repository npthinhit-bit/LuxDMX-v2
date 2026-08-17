# Core Stats — Technical Reference

| Attribute | Value |
|---|---|
| **Layer** | `core` |
| **Module** | `stats` |
| **Source Files** | `src/core/stats.h`, `src/core/stats.cpp` |
| **State Struct** | `StatsState` — single global instance |
| **Related** | [WebSocket Frame Builder](net-ws-frame.md), [Web Routes](net-web-routes.md), [DMX Buffer](core-dmx-buffer.md) |

## 1. File Inventory

| Source File | Purpose |
|---|---|
| `src/core/stats.h` | `StatsState` struct, `stats()` accessor, inline helpers (`outFpsLive`, `inFpsLive`, `uptimeSec`), `maybeLog` declaration |
| `src/core/stats.cpp` | Global `g_stats` instance, `stats()` accessor, `maybeLog` implementation |

## 2. Global State

```cpp
static StatsState g_stats;          // src/core/stats.cpp:11
StatsState& stats() { return g_stats; }  // src/core/stats.cpp:12
```

- Single static instance, zero-heap.
- `stats()` returns a reference — all modules call `stats()` to read/write.

## 3. StatsState Struct (`src/core/stats.h:19`)

### 3.1 Frame Counting
| Field | Type | Source | Description |
|---|---|---|---|
| `frameCount` | `uint32_t` | `src/core/stats.h:20` | Total frames processed since boot |
| `lastFrameMs` | `uint32_t` | `src/core/stats.h:21` | Last frame timestamp |
| `fps` | `float` | `src/core/stats.h:22` | Computed frame rate (FPS) |

### 3.2 Per-Output Input Stats
| Field | Type | Source | Description |
|---|---|---|---|
| `inFrameCnt[MAX_OUTPUTS]` | `uint32_t[4]` | `src/core/stats.h:23` | Input frame counter per output |
| `inWinMs[MAX_OUTPUTS]` | `uint32_t[4]` | `src/core/stats.h:24` | Input window start timestamp |
| `inFpsOut[MAX_OUTPUTS]` | `float[4]` | `src/core/stats.h:25` | Computed input FPS per output |

### 3.3 Per-Output Output Stats
| Field | Type | Source | Description |
|---|---|---|---|
| `outFrameCount[MAX_OUTPUTS]` | `uint32_t[4]` | `src/core/stats.h:26` | Output frame counter per output |
| `outLastFrameMs[MAX_OUTPUTS]` | `uint32_t[4]` | `src/core/stats.h:27` | Last output frame timestamp |
| `outLastDmxMs[MAX_OUTPUTS]` | `uint32_t[4]` | `src/core/stats.h:28` | Last DMX transmit timestamp |
| `outInSeq[MAX_OUTPUTS]` | `volatile uint32_t[4]` | `src/core/stats.h:29` | Seqlock in-progress counter (core safety) |
| `outFps[MAX_OUTPUTS]` | `float[4]` | `src/core/stats.h:30` | Output FPS |
| `txFrames[MAX_OUTPUTS]` | `volatile uint32_t[4]` | `src/core/stats.h:31` | Actual transmitted frame count (RMT) |
| `outTxFps[MAX_OUTPUTS]` | `float[4]` | `src/core/stats.h:32` | Computed TX FPS |
| `outSrcLost[MAX_OUTPUTS]` | `bool[4]` | `src/core/stats.h:33` | Source lost flag (initialized `true`) |

### 3.4 Input Counters (for loss detection)
| Field | Type | Source | Description |
|---|---|---|---|
| `rxFrameCount[MAX_OUTPUTS]` | `uint32_t[4]` | `src/core/stats.h:34` | Total received frames |
| `rxLossCount[MAX_OUTPUTS]` | `uint32_t[4]` | `src/core/stats.h:35` | Frame loss count |

### 3.5 Mode & Timing
| Field | Type | Source | Description |
|---|---|---|---|
| `manualMode` | `bool` | `src/core/stats.h:36` | Manual override mode (bypasses network input) |
| `jitterMs` | `float` | `src/core/stats.h:37` | Inter-packet jitter in ms |
| `prevFrameMs` | `uint32_t` | `src/core/stats.h:38` | Previous frame timestamp (jitter calc) |
| `startMs` | `uint32_t` | `src/core/stats.h:39` | Boot timestamp (`src/main.cpp:42`) |
| `lastDmxMs` | `uint32_t` | `src/core/stats.h:40` | Last DMX buffer write |
| `lastWsPush` | `uint32_t` | `src/core/stats.h:41` | Last WebSocket push timestamp |

### 3.6 Source Status & RDM Counters
| Field | Type | Source | Description |
|---|---|---|---|
| `srcStatus` | `volatile uint8_t` | `src/core/stats.h:42` | 0=normal, 1=conflict, 2=merging |
| `rdmSent` | `volatile uint32_t` | `src/core/stats.h:43` | Cumulative RDM commands sent |
| `rdmRecv` | `volatile uint32_t` | `src/core/stats.h:44` | Cumulative RDM responses received |
| `rdmCount` | `int` | `src/core/stats.h:45` | Number of discovered RDM devices |
| `rdmTod[RDM_TOD_MAX]` | `rdm_uid_t[64]` | `src/core/stats.h:46` | Table of discovered device UIDs |

### 3.7 Log Buffer
| Field | Type | Source | Description |
|---|---|---|---|
| `logBuf[LOG_BUF_CAP]` | `LogEntry[32]` | `src/core/stats.h:49` | Circular log of recent DMX changes |
| `logHead` | `uint8_t` | `src/core/stats.h:50` | Write position |
| `logCount` | `uint8_t` | `src/core/stats.h:51` | Current count (≤ 32) |
| `logLastMs` | `uint32_t` | `src/core/stats.h:52` | Last log write timestamp |

### 3.8 LogEntry Struct
```cpp
struct LogEntry {
    uint32_t ip;             // src/core/stats.h:48 — source IP
    uint8_t  proto;          // src/core/stats.h:48 — 0=ArtNet, 1=sACN
    uint8_t  data[8];        // src/core/stats.h:48 — first 8 bytes of changed channels
};
```

## 4. Inline Helpers

### 4.1 `outFpsLive`
```cpp
inline float outFpsLive(int i) { ... }  // src/core/stats.h:57
```
- Returns `stats().outTxFps[i]` if non-zero, else `stats().outFps[i]` (`src/core/stats.h:58-59`).
- Used by WebSocket frame builder at `src/net/ws_frame.cpp:64`.

### 4.2 `inFpsLive`
```cpp
inline float inFpsLive(int i) { return stats().inFpsOut[i]; }  // src/core/stats.h:62
```
- Used by WebSocket frame builder at `src/net/ws_frame.cpp:74`.

### 4.3 `uptimeSec`
```cpp
inline uint32_t uptimeSec() { return (millis() - stats().startMs) / 1000; }  // src/core/stats.h:66
```
- `startMs` set in `setup()` at `src/main.cpp:42`.
- Used by `wsBuildFrame` (`src/net/ws_frame.cpp:32`), `handleInfoJson` (`src/net/web_routes.cpp:91`), `handleHealth` (`src/net/web_routes.cpp:191`).

## 5. `maybeLog` — Change Log

Source: `src/core/stats.cpp:14`

```cpp
void maybeLog(int outIdx, const uint8_t* cur, uint16_t len, uint32_t ip, uint8_t proto);
```

### 5.1 Rate Limiting
- Only logs at most once every 100 ms per output (`src/core/stats.cpp:16`).
- `if (now - stats().logLastMs < 100) return;`

### 5.2 Circular Buffer
- Writes to `stats().logBuf[stats().logHead]` (`src/core/stats.cpp:18,21-25`).
- Increments `logCount` up to `LOG_BUF_CAP` (`src/core/stats.cpp:19`).
- Wraps `logHead` modulo `LOG_BUF_CAP` (`src/core/stats.cpp:20`).

### 5.3 Call Site
- Called from `frame_router.cpp:24` when the current output is being viewed:
  ```cpp
  if (i == viewOutput()) maybeLog(i, &dmxBufferState().buffers[i].data[1], 512, senderIp, proto);
  ```
  (Source: `src/core/frame_router.cpp:24` — only logs changes for the *monitored* output, not all outputs.)

## 6. Constants

| Constant | Value | Source |
|---|---|---|
| `CONFIG_LUXDMX_LOG_BUF_CAP` | 32 | `src/core/stats.h:7` |
| `LOG_BUF_CAP` | 32 (from CONFIG_LUXDMX_LOG_BUF_CAP) | `src/core/stats.h:9` |
| `LOG_FRAME_LEN` | 8 | `src/core/stats.h:10` |
| `CONFIG_LUXDMX_RDM_TOD_MAX` | 64 | `src/core/stats.h:15` |
| `RDM_TOD_MAX` | 64 (from CONFIG_LUXDMX_RDM_TOD_MAX) | `src/core/stats.h:17` |

## 7. Source Status Enum

Defined at `src/core/stats.h:12`:
```cpp
enum { SRC_NORMAL = 0, SRC_CONFLICT = 1, SRC_MERGING = 2 };
```

| Value | Name | Meaning |
|---|---|---|
| 0 | `SRC_NORMAL` | Single source or clean HTP/LTP merge |
| 1 | `SRC_CONFLICT` | Multiple unmanaged sources clashing |
| 2 | `SRC_MERGING` | Intentional merge (HTP/LTP configured) |

## 8. Cross-Core Access

| Field | Core 0 Access | Core 1 Access | Safety |
|---|---|---|---|
| `frameCount`, `fps`, `lastFrameMs` | Written in `frame_router.cpp` | Read in `wsBuildFrame` | Seqlock or volatile not needed (read-only on core 0) |
| `inFrameCnt`, `inWinMs`, `inFpsOut` | Written in `netRxTask` (core 0) | Read in `wsBuildFrame` (core 0) | Same core — no sync needed |
| `outFrameCount`, `outLastFrameMs`, `outLastDmxMs` | Read in `wsBuildFrame` (core 0) | Written in `dmxTxTask` (core 1) | `volatile` not used — seqlock in `dmxBufSnapshot` protects buffer reads |
| `outInSeq` | Read in `wsBuildFrame` (core 0) | Written in `dmxTxTask` (core 1) | `volatile` (`src/core/stats.h:29`) — seqlock coordination |
| `outFps`, `outTxFps` | Read in `wsBuildFrame` (core 0) | Written in `dmxTxTask` (core 1) | Approximate read is acceptable |
| `txFrames` | Read in `wsBuildFrame` (core 0) | Written in `snapshotAndTransmit` (core 1) | `volatile` (`src/core/stats.h:31`) |
| `outSrcLost` | Read in `wsBuildFrame`, `handleHealth` (core 0) | Written in `frame_router.cpp` (core 0) | Same core |
| `srcStatus` | Read in `wsBuildFrame` (core 0) | Written in merge engine (core 1) | `volatile` (`src/core/stats.h:42`) |
| `rdmSent`, `rdmRecv` | Read in `wsBuildFrame` (core 0) | Written in RDM task (core 1) | `volatile` (`src/core/stats.h:43-44`) |
| `rdmCount` | Read in `wsBuildFrame`, `handleRdmJson` (core 0) | Written in `rdmWsProcessQueued` (core 0) | Same core — but also RDM task writes, use with care |
| `rdmTod` | Read in `handleRdmTod` (core 0) | Written in `rdmWsProcessQueued` (core 0) | Same core for ws_handler; RDM task may also write — not `volatile` (potential race, logged in issue tracker) |
| `manualMode` | Written in `ws_handler` (core 0), `config_js` (browser via /config) | Read in `merge_engine` (core 1) | `stats().manualMode` not `volatile` — checked in `src/core/merge_engine.cpp:47` |
| `jitterMs`, `prevFrameMs` | Written in `netRxTask` (core 0) | Read in `wsBuildFrame` (core 0) | Same core |
| `startMs` | Written in `setup()` (core 0) | Read in `wsBuildFrame` (core 0) | Set once |
| `lastWsPush` | Read in `wsPush`? | Not currently used for enforcement | `src/core/stats.h:41` — reserved |

## 9. Integration with WebSocket Frame

`wsBuildFrame()` at `src/net/ws_frame.cpp:21-96` reads:

| Stats Field | Frame Offset | Source |
|---|---|---|
| `stats().fps` | wsBuf[0-1] | `src/net/ws_frame.cpp:26` |
| `stats().jitterMs` | wsBuf[14-15] | `src/net/ws_frame.cpp:33` |
| `activeSenderCount()` | wsBuf[12] | `src/net/ws_frame.cpp:41` |
| `stats().srcStatus` | wsBuf[13] | `src/net/ws_frame.cpp:42` |
| `dmxBufferState().buffers[i].data` | wsBuf[16+i*512] | `src/net/ws_frame.cpp:48` |
| `outFpsLive(i)` | wsBuf[2064+2*i] | `src/net/ws_frame.cpp:64-65` |
| `inFpsLive(i)` | wsBuf[2074+2*i] | `src/net/ws_frame.cpp:75` |
| `dmxIsDelta(i)` | wsBuf[2084+i] bit 0 | `src/net/ws_frame.cpp:82` |
| `cfg.outputs[i].txStyleSrc` | wsBuf[2084+i] bit 1 | `src/net/ws_frame.cpp:83` |
| `stats().rdmCount` | tail bytes 0-1 | `src/net/ws_frame.cpp:89` |
| `stats().rdmSent` | tail bytes 2-5 | `src/net/ws_frame.cpp:90-93` |
| `stats().rdmRecv` | tail bytes 6-9 | `src/net/ws_frame.cpp:94-95` |

## 10. Integration with Web Routes

### 10.1 `handleInfoJson` (src/net/web_routes.cpp:81)
- `uptimeSec()` → `uptime_s` field (`src/net/web_routes.cpp:91`)
- `ESP.getFreeHeap()` → `heap_free` field (`src/net/web_routes.cpp:92`)
- `netRSSI()` → `rssi` field (`src/net/web_routes.cpp:93`)
- `httpReqCount` → `http_reqs` field (`src/net/web_routes.cpp:99`)

### 10.2 `handleHealth` (src/net/web_routes.cpp:188)
- `stats().outSrcLost[i]` → `signal` and `source` fields (`src/net/web_routes.cpp:200-201`)
- `stats().rxFrameCount[i]` → `rx_frames` field (`src/net/web_routes.cpp:202`)
- `stats().rxLossCount[i]` → `rx_loss` field (`src/net/web_routes.cpp:203`)

### 10.3 `handleRdmJson` (src/net/web_routes.cpp:118)
- `stats().rdmCount` → `fixturesA` field (`src/net/web_routes.cpp:124`)
- `artNet().artPolls` → `artPolls` field (`src/net/web_routes.cpp:158`)

### 10.4 `handleRdmTod` (src/net/web_routes.cpp:469)
- Iterates `stats().rdmTod[0..rdmCount-1]` (`src/net/web_routes.cpp:471-473`)

## 11. Memory Model

| Component | Size (approx) | Allocation |
|---|---|---|
| `g_stats` (StatsState) | ~400 bytes | Static (`src/core/stats.cpp:11`) |
| `logBuf` | 32 × 13 bytes = 416 bytes | Static (within StatsState) |
| `rdmTod` | 64 × 6 bytes = 384 bytes | Static (within StatsState) |
| `inFrameCnt` | 4 × 4 = 16 bytes | Static |
| `outLastDmxMs` | 4 × 4 = 16 bytes | Static |
| `outInSeq` | 4 × 4 = 16 bytes (volatile) | Static |
| `txFrames` | 4 × 4 = 16 bytes (volatile) | Static |

- Total static footprint: < 1 KB.
- No heap allocation anywhere in `stats.cpp`.

## 12. Performance

- `stats()` is a simple reference return — zero overhead (`src/core/stats.cpp:12`).
- `outFpsLive` and `inFpsLive` are inline — zero call overhead.
- `uptimeSec()` is inline — zero call overhead.
- `maybeLog` rate-limited to 10 Hz max per output (`src/core/stats.cpp:16`).

## 13. Testing

- No direct host-native tests for `stats.h` / `stats.cpp`.
- `maybeLog` could be tested via the native runner with shims for `millis()`.

## 14. Configuration Exposure

- `manualMode` is set via WebSocket `{"mode":...}` command (`src/net/ws_handler.cpp:185`).
- `manualMode` is read by the merge engine at `src/core/merge_engine.cpp:47`.
- No `CFG_*` schema field for `manualMode` — it is a runtime-only toggle, not persisted.

## 15. Known Issues

- `rdmTod` and `rdmCount` are not `volatile` but are written from both core 0 (`rdmWsProcessQueued` → `src/main.cpp:150`) and potentially core 1 (RDM task). The comment at `src/core/stats.h:46` does not note this race.
- `logJson()` (`src/net/web_routes.cpp:551-558`) returns `"[]"` — the log buffer is populated by `maybeLog` but not serialized.

## 16. Startup Integration

- `stats().startMs = millis()` in `setup()` at `src/main.cpp:42` (phase 1).
- `StatsState` zero-initializes all fields by default (C++ value-initialization via `= {}` syntax at `src/core/stats.h:23-35`).

## 17. File Summary

`src/core/stats.h` declares:
- `StatsState` struct (full layout at `src/core/stats.h:19-53`)
- `stats()` accessor (`src/core/stats.h:55`)
- `outFpsLive()`, `inFpsLive()`, `uptimeSec()` inline helpers
- `maybeLog()` declaration (`src/core/stats.h:70`)

`src/core/stats.cpp` defines:
- `g_stats` static instance (`src/core/stats.cpp:11`)
- `stats()` accessor (`src/core/stats.cpp:12`)
- `maybeLog` implementation (`src/core/stats.cpp:14-26`)

## 18. Source Status Values

| Value | Enum | Used In |
|---|---|---|
| 0 | `SRC_NORMAL` | Merge engine normal state |
| 1 | `SRC_CONFLICT` | Unmanaged source clash |
| 2 | `SRC_MERGING` | Intentional HTP/LTP merge |

Definition: `src/core/stats.h:12`.

## 19. Log Buffer Structure

```
logBuf[0]  ┌─ ip (4B) + proto (1B) + data[8] (8B) = 13B
logBuf[1]  ├─ ip (4B) + proto (1B) + data[8] (8B) = 13B
...
logBuf[31] └─ (wraps)
```

- `LOG_FRAME_LEN = 8` — only first 8 channel values stored (`src/core/stats.h:10`).
- `LOG_BUF_CAP = 32` entries (`src/core/stats.h:9`).
- `logHead` wraps modulo 32 (`src/core/stats.cpp:20`).

## 20. References

- StatsState struct: [`src/core/stats.h:19`](src/core/stats.h#L19)
- Global instance: [`src/core/stats.cpp:11`](src/core/stats.cpp#L11)
- `maybeLog` rate limit: [`src/core/stats.cpp:16`](src/core/stats.cpp#L16)
- `startMs` initialization: [`src/main.cpp:42`](src/main.cpp#L42)
- `outFpsLive` inline: [`src/core/stats.h:57`](src/core/stats.h#L57)
- `uptimeSec` inline: [`src/core/stats.h:66`](src/core/stats.h#L66)
- `maybeLog` call site: [`src/core/frame_router.cpp:24`](src/core/frame_router.cpp#L24)
- WebSocket frame reads stats: [`src/net/ws_frame.cpp:26-95`](src/net/ws_frame.cpp#L26)
- Config field count: [`src/cfg/config_schema.cpp:133`](src/cfg/config_schema.cpp#L133)
- DMX buffer (for seqlock): [`include/seqlock.h:9`](include/seqlock.h#L9)
- RDM UID type: [`include/rdm_types.h`](include/rdm_types.h)
