# Frame Router - System Specification

Domain: core.frame-router

## 1. Module Overview

**Module ID:** core.frame-router
**Layer:** core (consumed by net layer on core 0; writes to core DMX buffer read by sys layer on core 1)

The Frame Router is the central dispatch point that routes a single decoded DMX universe frame from a network or local input source to every enabled output whose 15-bit port address matches the frame's universe. It is invoked once per inbound packet (or per committed staged frame) by the Art-Net and sACN protocol handlers, and by the converter-mode DMX input path.

For each inbound frame the router performs the following sequence:

1. Updates the Sender Tracker with the source frame metadata (IP, protocol, universe, priority, and decoded slot data).
2. Iterates all configured outputs; for each enabled output whose 15-bit port address matches the frame universe:
   a. Writes the frame into the seqlock-protected per-output DMX buffer (start code in data[0], slot bytes in data[1..512]).
   b. Invokes the Merge Engine to blend the updated source with other sources and produce the final merged frame in the buffer.
   c. If this output is the WebSocket monitor output, emits the frame to the stats/log subsystem for web display.
3. Mirrors the frame to additional outputs via the splitMask bitmask (universe splitting), repeating steps 2a-2b for each bit-set destination output.
4. Updates the source-status statistic (srcStatus) if at least one output matched.

The router is a **pure dispatch layer**: it performs no packet parsing, no protocol decoding, and no hardware access. It reads the resolved configuration at call time and writes only to the seqlock-protected DMX buffer via the merge engine. It holds **no persistent state** across invocations and performs **no heap allocation**.

**Consumers (callers):**
- Network protocol layer (Art-Net and sACN) on core 0
- Converter-mode DMX input path on core 0
- System transmit task reads buffers written by the router indirectly via the seqlock snapshot

**Delegates to:**
- core.sender-tracker - updateSender()
- core.merge-engine - mergeOutput()
- core.dmx-buffer - dmxBufWriteBegin / dmxBufWriteEnd (seqlock bracketing)
- sys.tasks - viewOutput() (monitor output selection)
- core.stats - stats().rxFrameCount[i], stats().srcStatus, maybeLog()

## 2. External Interfaces

### 2.1 Caller-Facing API

| Function | Caller | Trigger |
|---|---|---|
| routeFrame(artUniverse, data, length, senderIp, proto, priority) | Art-Net protocol handler, sACN protocol handler, converter-mode input router, ArtSync staged-commit path | Every ArtDMX packet, every direct sACN stream packet, every sACN sync-loss commit, every DMX-in frame in converter mode, every committed ArtSync staged frame |
| routeFrameNzs(artUniverse, data, length, startCode, senderIp, priority) | Art-Net protocol handler | Every ArtNzs opcode packet (non-zero start code) |
| portAddress(DmxOutput) (inline) | Merge engine, frame router, sACN protocol, ArtSync commit path | Computes the 15-bit output address for universe matching |

### 2.2 Function Signatures (Abstract)

```
void routeFrame(int universe, const uint8_t* data, uint16_t length,
                uint32_t senderIp, uint8_t proto, uint8_t priority);

void routeFrameNzs(int universe, uint8_t* data, uint16_t length,
                   uint8_t startCode, uint32_t senderIp, uint8_t priority);

uint16_t portAddress(const DmxOutput& o);
```

### 2.3 Protocol Discriminators

The proto parameter distinguishes the source protocol:
- 0 = Art-Net
- 1 = sACN

This discriminator is used solely for sender tracking and monitor logging; it does not affect routing logic. routeFrameNzs hardcodes proto = 0 (ArtNet) because sACN never carries a non-zero start code (non-zero-start-code sACN packets are dropped at the sACN layer).

### 2.4 Frame Size

A DMX frame is **513 bytes**: 1 start-code byte (data[0]) followed by 512 slot bytes (data[1..512]). Frame lengths exceeding 512 slot bytes are clamped to 512 before being written to any buffer.

### 2.5 Addressing

- **Art-Net universe** is a 15-bit value computed from three output-config fields: (net << 8) | (subnet << 4) | universe.
  - net range: 0-127
  - subnet range: 0-15
  - universe range: 0-15
- **sACN universe** (1-based on the wire) is converted to the 0-based Art-Net port address by subtracting 1 at the sACN layer before calling routeFrame.
- **sACN auto-universe**: when an output's sacnUniverse config field is 0, the sACN universe is derived as universe + 1 (converting the 0-based Art-Net universe to the 1-based sACN universe).

## 3. State Machine

No state machine. The Frame Router is a **stateless dispatch function**. Each call to routeFrame() or routeFrameNzs() processes one frame independently: find matching outputs, write each matching buffer, merge, split, update stats. No state is retained between calls.

## 4. Data Flow

The router is invoked in three contexts, all on core 0:

### 4.1 Network Input Path (Art-Net / sACN)

1. A UDP packet is received by the Network Receive task (core 0).
2. The protocol layer (Art-Net or sACN) decodes the packet header, extracting the universe number, source IP, E1.31 priority, and the 512 slot bytes.
3. **ArtSync gating check**: For Art-Net, if sync mode is active, the frame is staged to a per-output staging array instead of being routed; it is committed later by commitArtSyncStaged() when an ArtSync Sync packet arrives or the sync-loss timeout fires. Similarly, sACN Stream-Sync staging holds frames when a sync universe is configured.
4. The protocol layer calls routeFrame() (or routeFrameNzs() for ArtNzs) with the decoded universe, slot data, length, sender IP, protocol discriminator, and priority.
5. The router calls updateSender() to cache the source frame in the Sender Tracker.
6. The router iterates all outputs; for each enabled, matching output it writes the frame to the seqlock-protected buffer and calls mergeOutput().
7. If the output has a non-zero splitMask, the frame is mirrored to each bit-set destination output (see 4.2 below).

### 4.2 Universe Splitting (splitMask)

When an output's splitMask bitmask is non-zero, each bit j (0-based output index) indicates that output j should also receive a copy of this frame.

1. After writing and merging the primary output i, the router tests each bit: splitMask & (1 << j).
2. For each set bit where output j is enabled and j != i, the router writes the same frame to buffers[j] via the seqlock bracket and calls mergeOutput(j).
3. The split destination outputs are not re-routed through the main universe-matching loop - they are written only via this mirror path. No updateSender() call is made for split destinations; the sender entry is already cached from the primary match.

### 4.3 Converter-Mode DMX Input Path

When an output is configured in converter mode (inputMode = retransmit to network), received DMX data on the input UART is fed back through the same routing interface:
1. The input router calls updateSender() with the output's own port address as the universe.
2. The input router calls routeFrame() with the received DMX frame, default priority 100, and protocol = Art-Net.
3. The router processes this identical to a network-sourced frame.

### 4.4 Monitor Logging

If a matched output's index equals the configured monitor output (viewOutput()), the router calls maybeLog() to push the frame to the statistics/logging subsystem for WebSocket display. A 100 ms throttle is enforced inside maybeLog() to limit serial and network bandwidth.

### 4.5 Post-Routing

After all matching outputs are processed, if at least one output matched, the router sets stats().srcStatus = sourceStatus() to update the source-conflict/merging indicator consumed by the WebSocket status push. If no output matched, no buffer is written and no statistic is updated.

## 5. Configuration Integration

### 5.1 Config Fields Read

All fields are read from cfg.outputs[i] (the resolved DmxOutput array). The router performs no config writes.

| Field | Scope | Description |
|---|---|---|
| enabled | Per-output | Gates whether the output participates in matching (enabled == false skips the output). |
| universe | Per-output | Low 4 bits of the 15-bit port address (range 0-15 within subnet). |
| net | Per-output | High bits of the 15-bit port address (range 0-127). |
| subnet | Per-output | Middle 4 bits of the 15-bit port address (range 0-15). |
| splitMask | Per-output | Bitmask of additional output indices that mirror this output's frame. |
| loopback | Per-output | Virtual universe that should also receive this output's frame. |

### 5.2 Apply Semantics

All signal-level fields (universe, net, subnet, splitMask, loopback) are marked CFG_LIVE in the configuration schema - changes take effect on the next routed frame without a reboot. The router reads the resolved values at call time from the global cfg struct.

> **Note:** The configuration schema (config_schema.cpp) marks net and subnet as CFG_LIVE (via the OINT_L macro). If the intended behavior is that net and subnet changes require a reboot (because they alter the output's 15-bit port address and could cause transient misrouting), the schema should use OINT (CFG_REBOOT) for these two fields. The current code allows live changes, which take effect on the next routed frame. See Open Questions.

### 5.3 No Config Writes

The router does not modify cfg or call any config persistence, validation, or live-apply functions. Configuration staging, validation, and NVS persistence are handled entirely by the Config Engine module.

## 6. Lifecycle

- **Init:** No explicit init function. The router has no module-level state to initialize. Output configuration (cfg.outputs[]) is populated by outputInitAll() which is called from setup() during system boot.
- **Per-packet (network):** routeFrame() / routeFrameNzs() is called on every matching ArtDMX, ArtNzs, or sACN stream packet.
- **Per-commit (ArtSync / sACN sync):** commitArtSyncStaged() invokes the routing path indirectly via routeFrame() for each committed staged frame.
- **Per-tick:** The router is not called from the core-1 transmit task (dmxTxTask). The transmit task calls mergeOutputTimed() (via the merge engine) for timed loss detection and never invokes the router directly.
- **Shutdown:** None. The router holds no resources or handles.

## 7. Error Handling

| Condition | Behavior |
|---|---|
| No output matches the incoming universe | The matched flag remains false. No buffer is written, no merge is performed, no statistic is updated. No error is logged. |
| Frame length exceeds 512 slot bytes | Length is clamped to 512 before the buffer write. The start code is always written to data[0]. |
| Output is disabled | The output is skipped in both the primary matching loop and the split-mask mirroring loop. |
| Split destination output j == i | Skipped - an output never mirrors to itself. |
| Split destination output j is disabled | Skipped. |
| Monitor output (viewOutput()) | Returns a valid output index even if the monitor output is disabled, falling back to the first enabled output or output 0. The router does not check the fallback result for validity - it trusts the return value. |
| maybeLog() throttle | A 100 ms throttle is enforced inside maybeLog() in the stats subsystem, preventing excessive logging on high-frame-rate universes. |

The router returns void from all public functions - no error codes are propagated. All error visibility is through the stats subsystem (frame count, source status) and the seqlock retry/tear-skips counter in the buffer module.

## 8. Timing Constraints

| Constraint | Value | Notes |
|---|---|---|
| DMX packet size | 513 bytes | 1 start code + 512 slots. |
| Maximum outputs | 4 | Bounded by MAX_OUTPUTS; matches the 4 RMT TX channels on the ESP32-S3. |
| Primary matching loop | O(4) per call | Constant-time universe comparison per output. |
| Split-mask mirroring | O(4) additional per call | Worst case: all outputs mirrored to all other outputs. |
| Art-Net packet budget | 8 packets per 2 ms tick | Bounded in the network receive task. |
| sACN packet budget | 4 packets per socket per 2 ms tick | Bounded in the network receive task. |
| Maximum routeFrame calls per tick | 32 | 8 Art-Net packets x 4 outputs (worst case all match). |
| Execution core | Core 0 | netRxTask (priority 5) for network sources; loop() for DMX-in. |
| Priority | Best-effort | No hard deadline. The core-1 dmxTxTask (priority 19) is never blocked by the router. |

The router executes on core 0 at priority 5 (netRxTask) or in the main loop() for converter-mode input. It is never preempted by the core-1 transmit path because core 1 has higher priority and the cores run independently. The seqlock write bracket ensures the core-1 snapshot reader observes either the complete previous frame or the complete new frame - never a torn write.

## 9. Memory & Allocation Model

- **No heap allocation.** The router performs zero malloc / heap_caps_malloc / new calls.
- **Frame data:** The caller (protocol layer) allocates the decoded frame buffer. For ArtNzs, a 513-byte frame is stack-allocated in the Art-Net handler before calling routeFrameNzs(). The router does not allocate frame storage.
- **Buffer writes:** Frame data is written via memcpy into the pre-allocated, statically-allocated DmxBuffer.data array (in DRAM, not PSRAM). The seqlock write bracket adds two 32-bit locals on the stack.
- **No persistent state:** The router holds no module-level variables. All state is external - the global cfg struct, the sender tracker table, and the DMX buffer arrays.
- **Per-call stack:** Minimal (two 32-bit seqlock locals + loop indices), well within the 16 KB netRxTask stack budget.

## 10. Safety Considerations

- **Tear-free buffer writes:** Every buffer write is bracketed by dmxBufWriteBegin() (seqlock ticket odd) and dmxBufWriteEnd() (seqlock ticket even, with full memory barrier). The core-1 reader (dmxBufSnapshot) detects in-progress writes via the odd ticket and retries up to 8 times before falling back to the last known-good frame.
- **No hardware timing on the path:** The router runs on core 0 at priority 5. It never directly drives the RMT peripheral or any GPIO. Hardware timing is the exclusive responsibility of the core-1 dmxTxTask and the RMT driver.
- **No frame loss amplification:** If a frame matches no output, it is silently dropped with no side effects. If a split-mirror destination is disabled, it is skipped without error.
- **Sender tracker ordering invariant:** updateSender() is called before any buffer writes, ensuring the sender table reflects the current frame before the merge engine reads it. This ordering is a correctness invariant - if the merge engine were called before the sender update, the new frame would not participate in the merge.
- **Sync staging delegation:** ArtSync and sACN Stream-Sync staging is handled in the network layer, not in the router. The router only sees committed frames. The router does not enforce ArtSync "hold" semantics (delaying non-synced frames) - this is enforced by the protocol layer checking sync mode before calling routeFrame().

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| core.sender-tracker | called by router | updateSender() - caches source frame before output matching |
| core.merge-engine | called by router | mergeOutput() - blends multiple sources and writes merged result to buffer |
| core.dmx-buffer | called by router (write) | dmxBufWriteBegin / dmxBufWriteEnd - seqlock-bracketed buffer write |
| core.dmx-buffer | read by router (staging) | dmxBufferState() buffers, staged[], sacnStaged[] - staging arrays written by protocol layer |
| core.output-init | called by router (read) | viewOutput() - selects the monitor output for logging |
| core.stats | written by router | stats().rxFrameCount[i], stats().srcStatus - per-output frame count and source status |
| core.stats | called by router | maybeLog(outIdx, data, len, ip, proto) - throttled monitor logging |
| cfg.config-schema | read by router | cfg.outputs[i] fields: enabled, universe, net, subnet, splitMask, loopback |
| net.artnet | calls router | routeFrame() / routeFrameNzs() - ArtDMX, ArtNzs, and ArtSync commit paths |
| net.sacn | calls router | routeFrame() - direct sACN stream and sync-loss commit paths |
| core.input-router | calls router | routeFrame() - converter-mode DMX-in path |
| sys.tasks | reads router output | dmxBufSnapshot() via snapshotAndTransmit() - core-1 snapshot of the buffer written by the router |

## 12. Testing Verification

| Test Case | File:Line | Validates |
|---|---|---|
| HTP merge via router path | test/native/merge_test.cpp:26-43 | updateSender + mergeOutput sequence (replicates routeFrameImpl order) produces correct per-channel max |
| OFF / last-frame-wins | test/native/merge_test.cpp:45-61 | Same-sender update path and last-seen wins |
| Cross-universe isolation | test/native/merge_test.cpp:78-92 | Only matching-port-address outputs are selected |
| LTP-Takeover priority | test/native/merge_test.cpp:94-116 | Priority-based sender selection after updateSender |
| Priority merge | test/native/merge_test.cpp:118-136 | Top-priority sender contributes; lower-priority sources are excluded |
| Source-loss timeout | test/native/merge_test.cpp:138-153 | 2500 ms default timeout |
| Source-loss timeout override | test/native/merge_test.cpp:155-169 | Per-output failsafeTimeout override |
| LOSS_PRESET fallback | test/native/merge_test.cpp:171-183 | Scene recall on LOSS_PRESET |
| LOSS_HOME fallback | test/native/merge_test.cpp:185-196 | Home scene recall on LOSS_HOME |
| SeqLock integrity | test/native/seqlock_test.cpp | dmxBufWriteBegin/WriteEnd bracket produces tear-free snapshots (100 write-read cycles, zero corruption) |
| Packet size constant | test/native/rdm_types_test.cpp | DMX_PACKET_SIZE == 513 |

**Untested paths:**
- The splitMask universe-splitting path has no unit test coverage.
- The routeFrameNzs (ArtNzs) path has no host test coverage.
- The ArtSync staged-commit path (commitArtSyncStaged) has no host test coverage.
- The converter-mode DMX-in path (inputRouterPoll -> routeFrame) has no host test coverage.
- No dedicated frame_router_test.cpp exists - the router is a thin dispatch layer with no branch logic that can be tested without network-layer packet parser mocks.

## 13. Open Questions

1. **Loopback field not consumed:** The loopback config field exists in DmxOutput (documented as "virtual universe to also receive this output's frame, 0 = none"), but the current frame router implementation does not read or act on it. Either the loopback mirroring path was intended but not yet implemented, or it is handled in a module not visible in the inspected source. This should be resolved - either implement the loopback mirroring path (analogous to splitMask) or remove the field.

2. **net/subnet CFG flags:** The configuration schema marks net and subnet (and universe, splitMask, loopback) as CFG_LIVE via the OINT_L macro. If the design intent is that net or subnet changes require a reboot (because they alter the output's 15-bit port address and could cause transient misrouting during an active show), the schema should use OINT (CFG_REBOOT) for these two fields. The current code allows live changes that take effect on the next routed frame.

3. **Split-mask merge-order ambiguity:** If output j is both a direct universe match and a split-mirror target of output i in the same frame iteration, the router writes to output j twice (once in the primary loop, once in the split loop). Both writes are bracketed by independent seqlock writes, so no data corruption occurs, but mergeOutput runs twice for output j and rxFrameCount may be incremented twice. Whether this is intentional is unknown.

4. **Staged array race documentation:** ArtSync and sACN Stream-Sync staging arrays are written and committed on core 0 within the serialized netRxTask context, but this serialization invariant (that staging and commit never overlap) is not formally documented as a locking contract in the source. If a future refactor introduces staging and committing on different cores or tasks, this would be a race.

5. **ArtSync hold semantics vs. splitMirror:** The router does not enforce ArtSync hold semantics (delaying transmission of non-synced frames). This is enforced by the Art-Net protocol layer checking syncMode before calling routeFrame(). The splitMask mirroring path bypasses this check - a split destination of a synced output would receive the frame immediately even if the primary output is in hold. Whether this is correct depends on whether split destinations should respect the primary output's sync state.

## 14. History

- **Initial router design:** Single routeFrame(universe, data, length, ip, proto, priority) dispatch from the Art-Net protocol layer, writing to matching outputs and calling mergeOutput().
- **ArtNzs support:** routeFrameNzs() was added to handle the Art-Net ArtNzs opcode, preserving the caller's non-zero start code in data[0] of the affected output buffers and hardcoding proto = 0 (ArtNet).
- **sACN integration:** Extended to accept sACN stream frames by converting the 1-based sACN universe to the 0-based Art-Net port address (universe - 1) before calling routeFrame().
- **splitMask mirroring:** The splitMask bitmask field was added to DmxOutput and universe-splitting logic was added to the router, allowing one universe to feed multiple physical outputs.
- **portAddress() refactor:** The 15-bit Art-Net port address computation (net << 8) | (subnet << 4) | universe was factored into an inline helper to replace an inline expression scattered across callers.
- **Sender tracking integration:** updateSender() was called first in the dispatch sequence (before output matching) to ensure the sender table is consistent when the merge engine reads it.
- **Source-status reporting:** stats().srcStatus = sourceStatus() was added to expose source-conflict and merging state to the WebSocket status push.
- **Monitor logging:** The viewOutput() check and maybeLog() call were added to push frames from the monitor output to the serial/web UI at a throttled 10 Hz.
- **Converter-mode input path:** The inputRouterPoll() function was connected to the router to feed DMX-in (converter mode) frames through the same routing pipeline as network-sourced frames.
- **CFG_LIVE tagging:** All signal-level fields (universe, net, subnet, splitMask, loopback) were marked CFG_LIVE so changes apply without a reboot. Whether net/subnet should require a reboot is under review (see Open Questions).
