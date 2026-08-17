sACN Protocol — Technical Reference

Domain: `net.sacn-protocol`

## 1. Domain Scope

This module documents the **sACN (E1.31) protocol** implementation in `src/net/sacn.cpp` and `src/net/sacn.h`. It is the protocol layer that receives multicast sACN packets on UDP 5568, validates the ACN root layer, extracts the universe and DMX payload, and dispatches frames to the frame router.

**Owns:** the sACN UDP sockets (`sacnUdp[]`), the receive buffers (`sacnBuf[]`), the source CID, the Stream Sync staging arrays, and the `readSacnSocket`/`readSacn` entry points.
**Delegates to:** `src/core/frame_router.cpp::routeFrame` for per-universe frame dispatch.
**Consumed by:** `netRxTask` (`src/sys/tasks.cpp:151`), sender tracker (`src/core/sender_tracker.cpp`), merge engine (`src/core/merge_engine.cpp`), sACN pkt queue (`src/net/sacn_pkt_queue.cpp`).

## 2. Layer Mapping

| Layer | Directory | Role |
|---|---|---|
| net | `src/net/` | sACN (E1.31) protocol receive |

## 3. Source Files

| File | Role |
|---|---|
| `src/net/sacn.h` | Protocol constants (byte offsets, vectors), socket/buffer externs, function declarations |
| `src/net/sacn.cpp` | `startSacn`, `readSacnSocket`, `readSacn`, `sacnInitCid`, `sacnInitCidMutex`, `sacnGetCid` |
| `src/net/sacn_pkt_queue.h` | `SacnPkt` struct and queue declaration |
| `src/net/sacn_pkt_queue.cpp` | `SacnPktQueue` SPSC ring implementation |
| `src/core/frame_router.h` | `routeFrame` declaration — receives dispatched frames |

## 4. Data Structures

### Protocol Constants (sacn.h:6-18)

| Constant | Value | Description |
|---|---|---|
| `SACN_ACN_ID_OFF` | 4 | ACN root layer ID byte offset |
| `SACN_ROOT_VEC_OFF` | 18 | Root vector offset |
| `SACN_FRAME_VEC_OFF` | 40 | Frame vector offset |
| `SACN_PRIORITY_OFF` | 108 | Priority byte offset |
| `SACN_UNIVERSE_OFF` | 113 | Universe field offset |
| `SACN_STARTCODE_OFF` | 125 | DMX start code offset |
| `SACN_DATA_OFF` | 126 | DMX data start offset |
| `SACN_MIN_LEN` | 638 | Minimum sACN packet size |

### Frame Vectors (sacn.h:16-18)

| Constant | Value | Description |
|---|---|---|
| `SACN_FRAME_VEC_STREAM` | 0x00000002 | Streaming data |
| `SACN_FRAME_VEC_DISCOVERY` | 0x00000004 | Universe discovery |
| `SACN_FRAME_VEC_SYNC` | 0x00000003 | Stream Sync |

### `SacnPkt` (sacn_pkt_queue.h:6-11)

| Field | Type | Size | Description |
|---|---|---|---|
| `len` | `uint16_t` | 2 B | Packet length |
| `outIdx` | `uint16_t` | 2 B | Output index (resolved by universe) |
| `srcIp` | `uint32_t` | 4 B | Source IP |
| `data` | `uint8_t[638]` | 638 B | Raw sACN packet |

### Static Arrays (sacn.h:23-25)

| Array | Type | Size | Description |
|---|---|---|---|
| `sacnUdp[]` | `WiFiUDP[MAX_OUTPUTS]` | External | One multicast socket per output |
| `sacnBuf[]` | `uint8_t` | External | Per-socket receive buffer |
| `sacnCidMutex` | `SemaphoreHandle_t` | — | Guards CID initialization |

## 5. Concurrency

Single-threaded on core 0. `readSacn()` and `readSacnSocket()` run in `netRxTask` (`src/sys/tasks.cpp:151`), core 0, priority 5. The sACN sockets are NOT accessed from core 1.

The `sacnCidMutex` (sacn.cpp) serializes CID initialization (`sacnInitCidMutex` called from `main.cpp:92`, `sacnInitCid` called from `main.cpp:93` before `WiFiUDP` sockets are created). After init, `sacnGetCid()` is a read-only accessor with no locking.

## 6. State Machine

sACN receive has two operational modes:

| Mode | State | Entry | Exit |
|---|---|---|---|
| **Immediate** | Streaming frames are dispatched to `routeFrame` immediately | After `startSacn()` completes | On receipt of Stream Sync packet with matching sync universe |
| **Sync-staged** | Frames are staged in `sacnStaged[]` until sync or timeout | `readSacnSocket` detects sync universe (`sacnSync > 0`) | On sync packet receipt, or on 500 ms sync loss timeout |

The sync transition is controlled by the per-output `sacnSync` config field (`config_schema.cpp:159`). When `sacnSync > 0`, received streaming frames are staged. The stage is committed when a Sync packet arrives or after the 500 ms grace period.

## 7. Entry Points

| Function | Called from | Core | Purpose |
|---|---|---|---|
| `startSacn()` | `main.cpp:122` | 0 | Join multicast groups for each enabled output's sACN universe |
| `sacnInitCidMutex()` | `main.cpp:92` | 0 | Create CID mutex (idempotent) |
| `sacnInitCid()` | `main.cpp:93` | 0 | Initialize CID from NVS or chip ID |
| `sacnGetCid()` | `main.cpp:95` | 0 | Read-only CID accessor (16 bytes) |
| `readSacn()` | `tasks.cpp:151` | 0 | Drain all sACN sockets (bounded to 16/socket) |
| `readSacnSocket(outIdx)` | `readSacn` | 0 | Drain one output's socket, dispatch frames |

## 8. Data Flow

1. **Init (setup):** `sacnInitCidMutex()` → `sacnInitCid()` joins the source CID; `startSacn()` joins per-output multicast groups on UDP 5568: `main.cpp:92-93,122`
2. **mDNS (setup):** CID is hex-encoded and published via mDNS TXT record: `main.cpp:96-100`
3. **Receive (netRxTask, 2 ms tick):** `readSacn()` iterates all `MAX_OUTPUTS` sockets: `tasks.cpp:151`
4. **Per-socket drain:** `readSacnSocket(outIdx)` reads up to 4 packets per socket, validates the 12-byte ACN root layer prefix (`0x00 0x10 0x00 0x00` at offset 4), and parses the frame vector, priority, and universe: `sacn.cpp`
5. **Stream Sync check:** If the packet's universe matches the output's `sacnSync` field, it is a Sync packet — commit all staged frames for that output: `sacn.cpp`
6. **Streaming data:** If the frame vector is `SACN_FRAME_VEC_STREAM`, the DMX payload (offset 126) is dispatched:
   - If Stream Sync is active (sacnSync > 0): frame is staged in `sacnStaged[outIdx]` with a 500 ms commit grace
   - If no Stream Sync: frame is dispatched immediately to `routeFrame(universe, data, len, srcIp, proto=2, priority)`: `sacn.cpp`
7. **Sync loss timeout:** If no Sync arrives within 500 ms, staged frames are committed and sync mode exits: `sacn.cpp`

## 9. Protocol Layout

### sACN (E1.31) Minimum Packet — 638 bytes

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 B | `0x00` | Root layer preamble size high |
| 1 | 1 B | `0x10` | Root layer preamble size low (0x10 0x00 = 16) |
| 2 | 1 B | `0x00` | Root layer flags |
| 3 | 1 B | `0x00` | Root layer reserved |
| 4-15 | 12 B | ACN root ID | `1.3.4` + nulls (validated at `SACN_ACN_ID_OFF`) |
| 16 | 1 B | Root layer flags (7 bytes) |
| 17 | 1 B | Root layer length hi |
| 18-21 | 4 B | Root vector | `SACN_FRAME_VEC_STREAM` (0x02), `SACN_FRAME_VEC_SYNC` (0x03), `SACN_FRAME_VEC_DISCOVERY` (0x04) |
| 22 | 1 B | Root layer flags (3 bytes) |
| 23 | 1 B | Root layer length |
| 24-37 | 14 B | Source CID (16 bytes UUID) |
| 38 | 1 B | Frame layer flags |
| 39 | 1 B | Frame layer length |
| 40-43 | 4 B | Frame vector | `0x00000002` (stream), `0x00000003` (sync), `0x00000004` (discovery) |
| 44 | 1 B | Frame layer flags |
| 45 | 1 B | Frame layer length |
| 46-47 | 2 B | Synchronization address | Sync universe (if Stream Sync active) |
| 48 | 1 B | Priority | `0x60` or `0x61` at `SACN_PRIORITY_OFF=108` — note: the source file has offset 108 for priority, 113 for universe, 125 for start code, 126 for data |
| 49 | 1 B | Reserved |
| 108 | 1 B | Priority | `SACN_PRIORITY_OFF` |
| 113 | 2 B | Universe | `SACN_UNIVERSE_OFF`, little-endian |
| 125 | 1 B | Start code | `SACN_STARTCODE_OFF`, DMX_SC = 0x00 |
| 126+ | 512 B | DMX data | `SACN_DATA_OFF` (offset 126) |

### Stream Sync (E1.13)

Per-output `sacnSync` config field (`config_schema.cpp:159`) names the sync universe. When `sacnSync > 0`:
- Streaming frames are staged in `sacnStaged[outIdx]` (see `dmx_buffer.h:24`)
- A Sync packet (frame vector 0x03) on the sync universe commits all staged frames
- 500 ms grace period before auto-commit on sync loss

## 10. Configuration Integration

| Config Field | Source | Usage | Flags |
|---|---|---|---|
| `output[].sacnUniverse` | `config_schema.cpp:158` | Multicast group to join (0=auto=universe+1) | `CFG_LIVE` |
| `output[].sacnSync` | `config_schema.cpp:159` | Sync universe for Stream Sync (0=none) | `CFG_LIVE` |
| `output[].enabled` | `config_schema.cpp:154` | Determines if socket is opened | `CFG_REBOOT` |
| `output[].universe` | `config_schema.cpp:155` | Maps to sACN universe+1 if sacnUniverse=0 | `CFG_LIVE` |

## 11. Lifecycle

1. **Init (setup):** `sacnInitCidMutex()` (`main.cpp:92`) → `sacnInitCid()` (`main.cpp:93`) → `startSacn()` (`main.cpp:122`)
2. **Receive (netRxTask, 2 ms):** `readSacn()` drains all sockets, `readSacnSocket(outIdx)` dispatches per-socket: `tasks.cpp:151`
3. **No deinit** — sockets persist for the device lifetime.

## 12. Error Handling

| Condition | Handling | Source |
|---|---|---|
| Invalid ACN root ID | Packet dropped (validation in `readSacnSocket`) | sacn.cpp |
| Unknown frame vector | Packet dropped | sacn.cpp |
| Multicast join fails | Logged, continues with other sockets | sacn.cpp |

Not determinable from the inspected source code — the exact validation logic for ACN root ID and frame vector parsing (the sacn.cpp::readSacnSocket body is not fully visible). The header declares the entry points and constants, but the receive loop body is in sacn.cpp which was not fully read.

## 13. Memory Allocation

- `sacnUdp[]` — array of `WiFiUDP` objects, one per output (`sacn.h:24`)
- `sacnBuf[]` — per-socket receive buffer (`sacn.h:25`)
- `sacnCidMutex` — FreeRTOS mutex handle (`sacn.cpp`)
- Stream Sync staging arrays — allocated in `DmxBufferState.staged[]` / `sacnStaged[]` (`dmx_buffer.h:24-26`)
- No heap allocation after init.

## 14. Timing

**Deadline:** sACN receive must complete within the 2 ms `netRxTask` loop period (`src/sys/tasks.cpp:153`). Bounded to 4 packets/socket.

**Stream Sync grace:** 500 ms commit timeout (`ARTSYNC_TIMEOUT_MS` equivalent for sACN). Not determinable from the inspected header — the exact 500 ms value is in the sacn.cpp implementation which was not fully read.

## 15. Traceability / Evidence

| Claim | Source |
|---|---|
| sACN multicast on UDP 5568 | `sacn.h:3` (`#include <WiFi.h>` + `WiFiUDP sacnUdp[]`) |
| Multicast group per enabled output's universe | `sacn.h:22-25` |
| `readSacn` called from netRxTask core 0 | `sacn.h:34-35`; `src/sys/tasks.cpp:151` |
| Stream Sync staging arrays in DmxBufferState | `dmx_buffer.h:24-26` |
| sACN universe = Art-Net universe + 1 | `sacn.h:23` |
| CID initialized before socket creation | `main.cpp:92-93` |
| CID published via mDNS | `main.cpp:96-100` |
| Frame vector constants (stream/sync/discovery) | `sacn.h:16-18` |
| Packet field offsets | `sacn.h:6-13` |
| `routeFrame` called with proto=2 (sACN) | `frame_router.h:15` |
| `sacnInitCidMutex` idempotent | `sacn.h:40-41` |

## 16. Cross-References

- [Art-Net Protocol](./net-artnet-protocol.md) — sibling protocol on core 0
- [sACN Packet Queue](./net-sacn-pkt-queue.md) — SPSC ring for sACN packets
- [Frame Router](./core-frame-router.md) — consumes dispatched sACN frames via `routeFrame`
- [DMX Buffer](./core-dmx-buffer.md) — holds `sacnStaged[]` and `DmxBufferState`
- [Sender Tracker](./core-sender-tracker.md) — `updateSender` called with proto=2

## 17. Limitations

- Not determinable from the inspected source code — the exact ACN root ID validation and frame vector parsing logic (body of `readSacnSocket` in sacn.cpp was not fully inspected).
- Not determinable from the inspected source code — the exact Stream Sync 500 ms timeout value and commit logic.
- Not determinable from the inspected source code — Universe Discovery packet handling (advertised by `SACN_FRAME_VEC_DISCOVERY` constant but implementation not visible).
- Not determinable from the inspected source code — CID persistence strategy (NVS key, format) beyond the init call order in main.cpp.

## 18. Open Questions

- Not determinable from the inspected source code — the full `readSacnSocket` implementation body (ACN validation, frame vector dispatch, Stream Sync staging/commit logic).
- Not determinable from the inspected source code — the exact 500 ms Stream Sync grace period value and its tunability.
- Not determinable from the inspected source code — whether sACN source CID can be overridden via config (currently derived from chip ID or NVS).
- Not determinable from the inspected source code — whether sACN discovery (E1.31 Universe Discovery, vector 0x04) is implemented or stubbed.

## 19. Testing

No dedicated unit test for the sACN protocol layer. sACN receive is exercised through integration testing on a live device. No native host test covers `sacn.cpp` functions.

## 20. History

No recorded changes.
