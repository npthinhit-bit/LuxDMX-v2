# WebSocket Protocol Subsystem Ã¢â‚¬â€ System Specification

## 1. Scope and Purpose

This document specifies the WebSocket protocol subsystem as a black box. It owns the bidirectional WebSocket server on the path `/ws`, maintaining up to 12 concurrent client connections. It pushes a fixed-layout binary status frame to subscribed clients at 10 Hz containing live system telemetry and DMX channel data, pushes a low-frequency JSON metadata frame at 2 Hz, and dispatches incoming text frames to the command handler. The binary frame is the sole wire format for status data; the text channel carries JSON commands from browser to device.

## 2. System Context

The WebSocket subsystem operates on the network layer of the dual-core architecture. It is consumed by the main loop on core 0, which invokes the frame push and metadata push entry points on each iteration. It reads telemetry and DMX data from the core layer (stats, sender tracking, seqlock-protected DMX buffers, per-output frame counters). It delegates all incoming text-frame commands to the command handler. It is initialized during web-server setup and registered on the HTTP server instance. The browser connects from the web frontend using the URL `ws://<host>/ws`.

**External consumers:**
- Command handler Ã¢â‚¬â€ receives all decoded text frames for dispatch.
- Main loop Ã¢â‚¬â€ drives both push entry points at fixed intervals.

## 3. Client Connection Management

The server accepts WebSocket upgrade requests on the path `/ws`. Each connecting client is assigned to a slot by computing `clientId % 12`, capping the connection table at 12 entries. On connect, the slot is assigned the default subscription mask covering all configured universes, and the per-client frame sequence tracker is zeroed.

On disconnect, the slot client pointer is cleared and its subscription is zeroed, making the slot immediately reusable. Only clients in the connected state are eligible for frame delivery; any client not in this state is cleared from its slot and skipped.

The server rejects sends to clients whose write buffer is full, checked via the writable-state check before each binary push.

## 4. Binary Frame Layout

The binary frame is a fixed 2095-byte structure built into a single reusable buffer and pushed to all subscribed clients. The frame contains system telemetry, per-output DMX data, per-output statistics, a changed-universe bitmap, and navigation counters.

### 4.1 Header Fields (bytes 0Ã¢â‚¬â€œ15)

| Offset | Length | Field | Type | Description |
|---|---|---|---|---|
| 0 | 2 | FPS | uint16 | Fixed-point times 10 frames per second |
| 2 | 2 | Link Quality | int16 | WiFi RSSI (signed) or Ethernet link speed |
| 4 | 4 | Free Heap | uint32 | Current free heap in bytes |
| 8 | 4 | Uptime | uint32 | Seconds since boot |
| 12 | 1 | Sender Count | uint8 | Number of active DMX sources |
| 13 | 1 | Source Status | uint8 | 0 = normal, 1 = conflict, 2 = merging |
| 14 | 2 | Jitter | uint16 | Fixed-point times 10 inter-packet jitter in ms |

### 4.2 DMX Data Block (bytes 16Ã¢â‚¬â€œ2063)

2048 bytes of DMX channel data: 4 outputs times 512 channels. Each output occupies a contiguous 512-byte sub-block at offset `16 + (outputIndex * 512)`. Channel data is one byte per channel. Only channel values (bytes 1Ã¢â‚¬â€œ512 of each output block) are included; the start code byte is not transmitted.

### 4.3 Per-Output Statistics (bytes 2064Ã¢â‚¬â€œ2083)

20 bytes of per-output counters. Each output consumes 5 bytes: 2 bytes TX FPS, 2 bytes RX FPS, 1 byte TX style. The TX style byte is a bitmask where bit 0 indicates delta-mode-follows-input and bit 1 indicates the transmit source is Art-Net.

### 4.4 Changed-Universe Bitmap (byte 2084)

A single byte where bit *i* is set if universe *i DMX data changed since the last frame build. This bitmap drives delta-only delivery: a client receives a frame only if at least one of its subscribed universes changed.

### 4.5 Navigation Tail (bytes 2085Ã¢â‚¬â€œ2094)

10 bytes of navigation counters: 2 bytes for fixture count (uint16), 4 bytes for cumulative RDM commands sent (uint32), and 4 bytes for cumulative RDM responses received (uint32).

## 5. Frame Push Logic

The frame push entry point is invoked once per main-loop iteration. It checks whether at least one client is connected; if zero clients are connected, it returns immediately as a no-op.

When clients are present, it builds the binary frame, then iterates all 12 client slots. For each slot:
- If no client is connected, the slot is skipped.
- If the client is not in the connected state, the slot is cleared and skipped.
- The changed-universe bitmap is AND-ed with the client subscription mask. If the result is zero and the client frame sequence matches the current global frame sequence, the client is skipped (delta-only delivery).
- If the client can send (write buffer not full), the 2095-byte frame is transmitted as a binary message.

The global frame sequence counter is incremented once per frame build, enabling per-client delta detection.

## 6. Meta Push Logic

The metadata push entry point is invoked once per main-loop iteration, interleaved with frame pushes but at a 2 Hz effective rate. It checks whether at least one client is connected; if zero, it returns immediately. It also checks that the server has write capacity across all clients and that the free heap exceeds a minimum threshold; if either check fails, it returns without sending.

When all guards pass, it constructs a JSON text frame containing the object `{"meta": 1, "senders": <sender summary>, "log": <change log>}`. The sender summary enumerates all active sources with their protocol, universe, port, priority, and packet statistics. The change log payload is currently an empty array. The text frame is broadcast to all connected clients via a single multi-cast text send.

## 7. Text Frame Dispatch

Incoming text frames from any client are forwarded as opaque payloads to the command handler. The WebSocket subsystem performs no text-frame content parsing or interpretation; it delivers the raw byte payload and the originating client ID to the handler entry point. The handler is responsible for all JSON decoding, command routing, and response logic.

The subsystem does not send text responses on its own initiative; it only forwards inbound text frames. All outbound text traffic originates from the metadata push entry point.

## 8. Client Subscription Model

Each connected client carries a subscription mask: one bit per universe. A bit set to 1 means the client wishes to receive binary frames when that universe DMX data changes.

**Default subscription:** On connect, a client receives the subscription mask covering all configured universes (all four universe bits set).

**Subscription update:** The browser sends a JSON command containing a universe list. The command handler parses the universe array and computes a bitmask, which is stored per-client-slot. Only universes present in the list have their bits set; all others are cleared.

**Delivery filtering:** On each frame push, the global changed-universe bitmap is AND-ed with the client subscription mask. If the result is zero (no subscribed universes changed since the client last received a frame), the client is skipped entirely, reducing both CPU load and network bandwidth. Clients may independently subscribe to different universe subsets, and each receives only the data for their subscribed universes.

## 9. State Management

The subsystem maintains a single static state structure containing:

- A client pointer array of 12 entries, indexed by slot.
- A frame-sequence array of 12 entries, one per client, for delta detection.
- A per-client subscription bitmask array of 12 entries.
- A global frame sequence counter, incremented once per frame build.
- A changed-universe bitmap, set during frame build and consumed during push.

No heap allocation occurs during frame construction or push. All buffers are statically allocated at compile time.

## 10. Concurrency Model

The WebSocket subsystem executes entirely on core 0 within the main loop. Frame building reads telemetry from the stats module and DMX data from the seqlock-protected DMX buffers. The seqlock read ensures tear-free access to the DMX frame data even though the core-1 DMX transmit task writes to the same buffer concurrently.

The network receive task (also on core 0) delivers data to the DMX buffers ahead of the seqlock, so the WebSocket push always reads a consistent snapshot. The WebSocket server itself runs as an event handler on core 0; it never executes on the time-critical core-1 path.

No cross-core synchronization primitives are required for WebSocket operation; the only cross-core concern is the seqlock-protected DMX buffer read, which is handled by the existing buffer protocol.

## 11. Lifecycle

**Initialization:** During system setup, after the HTTP server is configured, the WebSocket server is registered on the server instance and its connection, disconnect, and data event callbacks are registered. This occurs once during the web-server setup phase and persists for the device lifetime.

**Runtime:** The push and metadata-push entry points are invoked on every main-loop iteration indefinitely. Client connections and disconnections are handled by the registered event callbacks as they occur.

**No teardown:** There is no explicit shutdown or cleanup path. The WebSocket server instance persists until the device reboots or loses power.

## 12. Error Handling

| Condition | Behavior |
|---|---|
| No clients connected | Frame push and meta push return immediately (no-op) |
| Client disconnected mid-iteration | Slot is cleared, subscription zeroed |
| Client not writable (write buffer full) | Client is skipped; frame is not sent, iteration continues |
| Client reconnected but old slot still holds stale reference | Slot is overwritten on next connect via modulo assignment |
| Frame sequence already delivered to client | Client is skipped (delta-only delivery) |
| Binary send fails (network error) | Send is skipped; iteration continues to next client |
| Meta push during low memory | Returns without sending to avoid OOM |
| Meta push when write buffer full | Returns without sending |

## 13. Resource Allocation

All subsystem state is statically allocated:

- Frame buffer: A single 2095-byte buffer reused for every push, no per-frame allocation.
- Client pointer array: 12 entries (pointer-sized each).
- Frame sequence array: 12 times 32-bit entries.
- Subscription bitmask array: 12 times 16-bit entries.
- Last-sent DMX cache: 4 times 512 bytes (2048 bytes total) for delta comparison against the current frame.
- Changed-universe bitmap: 1 byte.

No heap allocation occurs during frame construction, client management, or push. The total static footprint of all WebSocket buffers is under 2.2 KB plus pointer overhead.

## 14. Timing and Performance

| Metric | Constraint |
|---|---|
| Binary frame push rate | Exactly 10 Hz (100 ms interval) |
| Metadata push rate | Approximately 2 Hz (2000 ms interval) |
| Binary frame size | 2095 bytes per push |
| Maximum concurrent clients | 12 (hard limit via modulo slot assignment) |
| Per-client push check | writable-state check evaluated before each binary send |
| Delta detection | Per-client frame sequence compared against global frame sequence |
| Subscription filtering | AND of changed-universe bitmap and client subscription per push |
| Metadata heap guard | Free heap must exceed 40 KB and max-alloc must exceed 24 KB |
| Frame construction | Reads from seqlock-protected DMX buffer snapshot |