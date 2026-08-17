# DMX Buffer Specification

Domain: core.dmx-buffer

## 1. Module Overview

The DMX Buffer module owns all per-output DMX frame storage in the firmware. It sits at the center of the dual-core producer/consumer boundary: the network receive path (core 0) writes decoded DMX frames into the buffer, and the DMX transmit task (core 1) reads them back for hardware transmission.

The module provides four categories of capability:

- **Live frame buffers** - one seqlock-protected 513-byte DMX frame per output. A 32-bit ticket seqlock makes the 513-byte write appear atomic to the reader, so a partially written frame is never observed.
- **Write bracketing API** - a begin/end pair that wraps every full-frame write. The writer bumps the seqlock ticket to an odd value before the write and back to even after, so the reader can detect in-progress writes.
- **Snapshot reader** - a read-side function that copies the frame and retries on torn writes. After 8 consecutive failed retries it gives up, increments a tear counter, and signals the caller to hold the previous frame.
- **Staging arrays** - per-output holding areas for ArtSync and sACN Stream-Sync. Frames staged here are not transmitted until the appropriate sync or timeout event commits them into the live buffer.

The module is a pure data store. It performs no packet parsing, no source merging, and no hardware driving. It delegates nothing and is delegated nothing for persistence - all structures are statically allocated in DRAM and zero-initialized at startup.

**Consumers (by role):**
- Network packet handlers and the frame router - writers of live frames and staging arrays.
- The merge engine - writer of merged frames.
- The Art-Net handler - writer of ArtSync staged frames and committer via the same buffer.
- The sACN handler - writer of Stream-Sync staged frames and committer via the same buffer.
- The system transmit task - reader of live frames via the snapshot API.
- The converter-mode input path - writer of DMX-input-sourced frames through the same routing interface as network frames.

## 2. External Interfaces

### 2.1 Caller-Facing API

| Function | Direction | Signature (abstract) | Behavior |
|---|---|---|---|
| dmxBufWriteBegin | Write | (int outputIndex) | Bumps the output seqlock ticket to an odd value (mid-write) and issues a full memory barrier. Must be called before any data is written to that output live buffer. |
| dmxBufWriteEnd | Write | (int outputIndex) | Issues a full memory barrier and bumps the seqlock ticket back to even (stable). Must be called after all 513 bytes have been written to the output live buffer. |
| dmxBufWriteEndSet | Write | (int outputIndex, int channel, uint8_t value) | Writes a single channel byte directly. Silently drops writes where the channel index is out of range (must satisfy 1 <= channel < DMX_PACKET_SIZE). |
| dmxBufSnapshot | Read | (int outputIndex, uint8_t* outFrame) | Copies 513 bytes from the live buffer into the callers outFrame. Retries up to 8 times on detected torn writes. Returns true on a consistent snapshot, false after exhausting retries (in which case outFrame is unmodified). |
| commitArtSyncStaged | Write/Commit | (int outputIndex) | Atomically copies the outputs ArtSync staging array into the live buffer via the seqlock write bracket. |
| routeFrame / routeFrameNzs | Write | (universe, data, length, sender, proto, priority) | Distributes a decoded DMX frame to all matching output live buffers via the seqlock write bracket. |
| flushArtSyncStaged | Write/Commit | (void) | Commits all dirty ArtSync staging arrays to their live buffers. Called on the system task. |

### 2.2 Data Structures

| Structure | Fields | Description |
|---|---|---|
| DmxBuffer | data[513], seq | One per output. data[0] is the DMX start code; data[1..512] are the 512 slot values. seq is the 32-bit volatile seqlock ticket. |
| DmxBufferState | buffers[4], staged[4][513], stagedValid[4], stagedLen[4], sacnStaged[4][513], sacnStagedValid[4], sacnStagedLen[4], sacnSyncAddr[4], tornSkips | The global state singleton. buffers is the live seqlock-protected array. staged / stagedValid / stagedLen are the ArtSync staging fields. sacnStaged / sacnStagedValid / sacnStagedLen / sacnSyncAddr are the sACN Stream-Sync staging fields. tornSkips is a monotonic counter of persistent torn-read failures. |
| SeqLock | seq | The generic 32-bit ticket primitive. Odd = writer mid-update; even = stable. Uses full memory barriers on every read and re-read. |

### 2.3 Constants

| Constant | Value | Description |
|---|---|---|
| DMX_PACKET_SIZE | 513 | DMX frame size: 1 start code byte + 512 slot bytes. |
| MAX_OUTPUTS / DMX_OUTPUT_COUNT | 4 | Number of per-output buffer sets. Matches the 4 RMT TX channel limit on the ESP32-S3. |
| SeqLock retry count | 8 | Maximum snapshot retry iterations before a torn read is declared a failure. |

## 3. State Machine

The live frame buffers have no state machine - each write is fully bracketed by begin/end and the seqlock ticket fully captures the atomicity boundary. There is no retained state across calls.

The staging arrays use a minimal binary staging state per output.

### ArtSync Staging State (per output)

| State | Transition | Trigger |
|---|---|---|
| Idle (clean) | to Staged (dirty) | An ArtDMX frame arrives while ArtSync sync mode is active; the frame is copied to staged[i] and stagedValid[i] set to true. |
| Staged (dirty) | to Idle (clean) | commitArtSyncStaged(i) is called - the staged frame is committed to the live buffer or the staging array cleared. |

### sACN Stream-Sync Staging State (per output)

| State | Transition | Trigger |
|---|---|---|
| Idle (clean) | to Staged (dirty) | An sACN stream frame arrives while sacnSyncAddr[i] is set (non-zero); the frame is copied to sacnStaged[i] and sacnStagedValid[i] set to true. |
| Staged (dirty) | to Idle (clean) | A sync packet for the configured sync universe arrives, or the sync loss timeout (500 ms grace) elapses; the staged frame is committed or discarded. |

## 4. Data Flow

### 4.1 Network Input to Live Buffer Write (Core 0)

1. A network packet is received and decoded into a 513-byte DMX frame (start code + 512 slots).
2. The frame router evaluates which outputs match the packets universe.
3. For each matching output, the writer calls dmxBufWriteBegin(i), writes the start code to data[0] and up to 512 slot bytes to data[1..512], then calls dmxBufWriteEnd(i).
4. The merge engine may then overwrite the same buffer with a merged result from multiple sources, again bracketed by begin/end.
5. If universe splitting is configured, the frame is mirrored to additional outputs via the same begin/end protocol.

### 4.2 ArtSync Staging Commit Flow (Core 0)

1. When ArtSync sync mode is active, incoming ArtDMX frames bypass the live buffer and are copied to staged[i] with stagedValid[i] set.
2. The frame is held (not transmitted) until either an ArtSync Sync opcode arrives or the sync loss timeout (1000 ms) expires.
3. On commit, commitArtSyncStaged(i) brackets the copy from staged[i] into buffers[i].data[1..512] with the seqlock.
4. The flushArtSyncStaged() path on the system task provides the timeout fallback when no ArtSync arrives.

### 4.3 sACN Stream-Sync Staging Commit Flow (Core 0)

1. When sacnSyncAddr[i] is non-zero, incoming sACN frames are copied to sacnStaged[i] with sacnStagedValid[i] set (instead of the live buffer).
2. The frame is held until either a sync packet arrives for the configured sync universe or the sync loss grace period (500 ms) elapses.
3. On commit, the staged frame is routed into the live buffer via the frame routers routeFrame path (same seqlock write bracket).

### 4.4 Live Buffer Read - Snapshot (Core 1)

1. The system transmit task wakes on its 1 ms tick.
2. For each enabled and ready output, it calls dmxBufSnapshot(i, frame).
3. The snapshot reads the seqlock ticket, checks it is even (not mid-write), copies all 513 bytes, re-reads the ticket, and returns true if the two ticket reads match and were even.
4. On failure (torn read), it retries up to 8 times. If all 8 retries fail, tornSkips is incremented and the function returns false.
5. On failure, the caller holds the previous frame - no partial or corrupted frame is ever transmitted.

### 4.5 RMT Transmit Continuation (Core 1)

6. If the snapshot succeeded and the RMT channel is idle, the transmit task kicks the RMT peripheral with the 513-byte frame. The RMT hardware then clocks the entire waveform autonomously, so the CPU is not on the bit-timing path.

### 4.6 Converter Mode Path

7. In converter mode (DMX input to network retransmit), DMX input frames enter the same routeFrame dispatch and are written to the live buffers through the identical seqlock write bracket. The buffer module does not distinguish between network-sourced and local DMX-input-sourced frames - both use the same write protocol.

## 5. Configuration Integration

| Config field | Read by | Purpose |
|---|---|---|
| cfg.outputs[i].enabled | Frame router | Gates whether this outputs buffer is used at all. |
| cfg.outputs[i].universe / .net / .subnet | Frame router | Computes the 15-bit port address used for matching. |
| cfg.outputs[i].splitMask | Frame router | Triggers universe mirroring to additional output buffers. |
| cfg.outputs[i].lossMode | Merge engine | Determines the action (hold, zero, last) when a source is lost. |
| cfg.outputs[i].lossPreset | Merge engine | Preset value applied on source loss per lossMode. |
| cfg.outputs[i].mergeMode | Merge engine | Selects HTP or LTP merge of multiple senders. |
| cfg.outputs[i].sacnSync | sACN handler | Sets sacnSyncAddr[i] for Stream-Sync staging. |

All signal-level fields (universe, splitMask, lossMode, mergeMode) are marked live-apply: changes take effect on the next routed frame without a reboot. Pin/GPIO-bound fields (txPin, rxPin, rtsPin, UART port, output mode) require a reboot. The buffer module itself performs no live-apply or reboot logic - it reads the resolved configuration at call time and applies the values to the appropriate buffer or staging array. The buffer module never writes configuration fields.

## 6. Lifecycle

| Phase | Action | Notes |
|---|---|---|
| Startup (pre-init) | Crash-guard init | The NVS crash counter may disable outputs before any buffer use. |
| Startup (init) | Static zero-initialization | The global state singleton is zero-initialized as a static-duration object. All seqlock tickets start at 0 (even = stable). All staging validity flags start false. No explicit init function is called. |
| Runtime | Read/write on every frame cycle | Writers (core 0 network receive) invoke begin/write/end on every inbound packet. The reader (core 1 transmit task) invokes snapshot every 1 ms tick for every enabled output. |
| Config change | Live-apply of signal-level fields | Loss mode, merge mode, split mask, and universe changes apply on the next routed frame. |
| Shutdown / reboot | None | The module has no shutdown hook. Buffers persist in DRAM for the device lifetime. A reboot resets all state to zero-initialized values. |

## 7. Error Handling

| Operation | Failure mode | Behavior |
|---|---|---|
| dmxBufSnapshot | Torn read (writer in progress) | Retries up to 8 times. On exhaustion: increments tornSkips, returns false, leaves the output pointer unmodified. |
| dmxBufSnapshot (caller side) | Returns false | The transmit task skips this outputs transmit for one tick and holds the previously transmitted frame. No error is logged by this module. |
| dmxBufWriteEndSet | Channel index out of range | Silently drops the write. Only channels satisfying 1 <= channel < 513 are accepted. |
| No output matches a universe | routeFrameImpl | No buffer is written. No logging. |
| Frame length exceeds 512 | Network layer | Clamped to 512 before the buffer write. |
| Seqlock retry exhaustion | Persistent writer starvation | The tornSkips counter (a uint32_t) is incremented. No overflow handling exists; overflow would occur only after ~4 billion persistent failures. |

The module performs no logging of its own. All error visibility is through the tornSkips counter and the snapshot return value.

## 8. Timing Constraints

| Constraint | Value | Notes |
|---|---|---|
| DMX packet size | 513 bytes | 1 start code + 512 slots. |
| Number of outputs | 4 | Matches the 4 RMT TX channels on the ESP32-S3. |
| SeqLock snapshot retry count | 8 | Hardcoded maximum retry iterations. |
| Transmit tick | 1 ms | The core-1 transmit task runs every 1 ms via a periodic delay. Every enabled output is snapshotted each tick. |
| Torn-read hold | 1 ms | A failed snapshot holds the previous frame for exactly one tick - benign, as the DMX break/mark cycle is ~44 us per slot x 513 + break + MAB ~= 24.3 ms. |
| Snapshot worst-case copies | ~4 KB | 8 retries x 513 bytes. In practice retries are rare because the 1 ms transmit tick is much slower than the microsecond-scale write on core 0. |
| ArtSync sync loss timeout | 1000 ms | Staged frames are committed if no ArtSync Sync arrives within this window. |
| sACN sync loss grace | 500 ms | Stream-Sync staged frames are committed if no sync packet arrives within this window. |

The writer (core 0) is never blocked by the reader (core 1). The reader may block briefly (up to 8 memcpy iterations) but is bounded and never preempts the time-critical transmit path because the reader is the transmit path.

## 9. Memory & Allocation Model

- **Location**: The entire state singleton resides in a single statically-allocated global object in DRAM (g_dmxBufState). It is zero-initialized at startup per C++ static-duration guarantees.
- **Layout per output**: 513 bytes (live buffer) + 513 bytes (ArtSync staging) + 2 bytes (staged length) + 1 byte (staged valid) + 513 bytes (sACN staging) + 2 bytes (sACN staged length) + 1 byte (sACN staged valid) + 2 bytes (sACN sync address) = 1032 bytes per output.
- **Total footprint**: 4 outputs x 1032 bytes + 4 bytes (torn-skips counter) ~= 4.1 KB of statically-allocated DRAM.
- **No heap allocation**: The module performs zero malloc / heap_caps_malloc calls. All buffers are compile-time static.
- **Stack usage**: The snapshot reader uses a caller-provided 513-byte output buffer (typically stack-allocated in the transmit task). The seqlock retry logic uses two 32-bit locals on the stack.
- **No PSRAM**: The buffer is in fast DRAM, not PSRAM, because it is on the core-1 transmit critical path and the RMT peripheral cannot DMA from PSRAM directly.

## 10. Safety Considerations

- **Critical invariant**: A partially written DMX frame must never be transmitted. The seqlock guarantees this - the reader only copies when the ticket is even (stable) and re-validates after the copy. If the ticket changed during the read, the copy is discarded and retried.
- **Benign failure on persistent torn read**: If all 8 retries are exhausted, the snapshot returns false and the transmit task holds the previous complete frame. The device transmits the last known-good frame rather than corrupt data, and misses one 1 ms tick of new data - invisible at the DMX physical layer (frames are ~44 ms apart at 44 Hz).
- **Core isolation**: The writer runs exclusively on core 0 (netRxTask, priority 5) and the reader exclusively on core 1 (dmxTxTask, priority 19). Core-0 WiFi and network DMA activity cannot preempt the 1 ms transmit tick. This was the central fix for the break-corruption-under-load issue (originally issue #64).
- **Memory ordering**: Every seqlock read and re-read is bookended by a __sync_synchronize() full memory barrier, ensuring the ticket value and the frame data are observed in the correct order on the ESP32-S3 dual-core architecture.
- **Staging isolation**: ArtSync and sACN Stream-Sync staged arrays are not seqlock-protected because they are written and read exclusively on core 0 within the same serialized task context. This is safe by single-threaded access, not by synchronization primitive.
- **Static allocation**: No runtime memory allocation eliminates heap fragmentation and out-of-memory failure modes from the critical path.

## 11. Cross-Module Dependencies

| Module | Provides | Consumes |
|---|---|---|
| Frame Router | Decoded DMX frames (per output) | dmxBufWriteBegin / dmxBufWriteEnd for live buffer writes |
| Merge Engine | Merged DMX frames (per output) | dmxBufWriteBegin / dmxBufWriteEnd for merged buffer writes |
| Art-Net Handler | ArtSync-staged frames, ArtSync Sync events | staged[i] staging arrays; commitArtSyncStaged on commit |
| sACN Handler | Stream-Sync-staged frames, sync packets | sacnStaged[i] staging arrays; sync commit on sync-loss |
| Sender Tracker | Sender cache entries | The buffer does not call the sender tracker; the frame router mediates. |
| System Tasks | 1 ms tick cadence | dmxBufSnapshot to obtain a tear-free frame for RMT transmit |
| DMX RMT TX Driver | Hardware frame transmission | The 513-byte snapshot frame produced by dmxBufSnapshot |
| Output Init | Per-output readiness flags | The buffer does not read readiness; the transmit task gates on it. |
| Config Engine | Resolved cfg.outputs[i] fields | Universe, splitMask, lossMode, lossPreset, mergeMode (read by consumers) |

The buffer module depends only on the generic seqlock primitive, the static g_dmxBufState allocation, and the configuration struct fields read by its consumers. It has no dependencies on the network, persistence, or driver layers.

## 12. Testing Verification

| Test | Scope | What it verifies |
|---|---|---|
| seqlock_test (native) | Generic SeqLock primitive | Clean snapshot during active write, stable copy under concurrent load, 100 write-read cycles with zero corruption. |
| merge_test (native) | Merge engine to buffer path | dmxBufSnapshot returns true and produces a consistent 513-byte frame after the merge engine writes via the seqlock bracket. Covers HTP, OFF/LTP, LOSS_ZERO, cross-universe isolation, LTP-Takeover priority, and priority merge scenarios. |
| rdm_types_test (native) | Constants | Verifies DMX_PACKET_SIZE == 513. |

No dedicated dmx_buffer_test.cpp exists. The buffers seqlock semantics are validated indirectly through the generic seqlock_test and the merge_test integration path. The 513-byte stack frame buffer in the transmit tasks snapshot-and-transmit path is exercised only on hardware during soak-test monitoring.

## 13. Open Questions

1. **WebSocket manual channel control**: The dmxBufWriteEndSet function (single-channel write) is declared in the buffer interface, but the caller that invokes it for WebSocket-driven manual channel control is not visible in the inspected source. Whether this path is wired or dead is undetermined.
2. **ArtSync commit triggers**: It is undetermined whether flushArtSyncStaged() (the system-task timeout path) is the only commit trigger when no ArtSync arrives, or whether commitArtSyncStaged is also reachable from a WebSocket or serial-console command path.
3. **sACN sync address source**: The sacnSyncAddr[i] per-output sync universe value is set during network startup, but whether it derives solely from cfg.outputs[i].sacnSync configuration or can also be set dynamically at runtime is not resolved from the inspected source.
4. **Staging array race documentation**: The ArtSync and sACN staging arrays are not seqlock-protected - they rely on single-threaded core-0 serialization between the staging write and the commit read. This invariant is not formally documented in the source as a locking contract.

## 14. History

- **Issue #64 (core separation + RMT hardware TX)**: The seqlock-protected DMX buffer was introduced alongside the move from UART+GPTimer to RMT-based DMX transmission. The seqlock was added to fix the core-0 network-DMA contention that corrupted DMX break timing - it ensures the core-1 transmit task always reads a complete, consistent frame.
- **Initial seqlock design**: Single-writer (core 0 netRxTask) / single-reader (core 1 dmxTxTask) with 32-bit ticket protocol and __sync_synchronize() full memory barriers. The 8-retry limit and tornSkips counter were part of the initial design.
- **ArtSync staging**: The staged[] / stagedValid[] / stagedLen[] fields were added to DmxBufferState to support Art-Net ArtSync synchronized frame commit. Frames are held until an ArtSync Sync opcode arrives or the 1000 ms sync-loss timeout fires.
- **sACN Stream Sync staging**: The sacnStaged[] / sacnStagedValid[] / sacnStagedLen[] / sacnSyncAddr[] fields were added to DmxBufferState for E1.31 Stream-Sync support. Frames are held until a sync packet arrives or the 500 ms sync-loss grace elapses.
- **Static allocation**: The entire state singleton has always been a zero-initialized static DRAM object. No heap allocation has ever been introduced, even as the number of staging arrays grew.
