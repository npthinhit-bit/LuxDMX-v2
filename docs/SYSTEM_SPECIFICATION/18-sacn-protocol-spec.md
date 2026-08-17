# sACN (E1.31) Protocol Subsystem — System Specification

## 1. Scope and Purpose

This document specifies the sACN (Streaming ACN, E1.31) protocol subsystem as a black box. It owns the multicast sACN UDP sockets bound to port 5568 — one per enabled output — receiving sACN packets, validating the ACN root layer, extracting the universe and DMX payload, honoring Stream Sync (E1.13) deferral and commit semantics, and dispatching frames to the frame router for per-universe output routing. It is consumed exclusively by the network-reception task on core 0.

The subsystem does not transmit sACN, does not run on core 1, and performs no hardware access. It reads the source CID and per-output universe and sync-universe configuration, and is initialized before the network sockets are created.

## 2. System Context

The sACN subsystem sits at the network layer. It is driven by the system task scheduler, which calls its packet-drain entry point every 2 ms tick on core 0 (priority 5). It receives packets on the wire via one UDP socket per enabled output, each bound to the multicast group for that output's sACN universe on UDP port 5568.

It calls down into the core layer (frame router for DMX dispatch, DMX buffer for Stream-Sync staging) and reads runtime configuration from the config engine. Its source CID is published to controllers via mDNS TXT records by the system setup phase.

Upstream (callers): System task scheduler (2 ms netRxTask tick, core 0).
Delegates to: Core frame router for committed frame dispatch.
Consumed by: Merge engine, sender tracker, and frame router — indirectly driven via dispatched committed frames.

## 3. Packet Reception

The subsystem maintains one WiFiUDP multicast socket per enabled output. Each socket joins the IPv4 multicast group 239.255.<hi>.<lo> derived from the output's 1-based sACN universe, where hi = (universe >> 8) and lo = (universe & 0xFF). Multicast group membership is joined at initialization and persists for the device lifetime.

On each 2 ms tick, the receive loop iterates all output sockets. For each socket, it reads at most 4 packets into a fixed 638-byte receive buffer. The socket operates in non-blocking mode. The loop exits the current socket's drain early if the socket is not ready or recvfrom returns no data.

Each received packet is validated against the ACN root layer: the ACN root ID string must match the E1.31 magic prefix at the root identifier offset. Packets failing this validation are dropped silently. Valid packets are further checked for a minimum size; packets shorter than the minimum sACN packet size of 638 bytes are dropped.

## 4. Packet Dispatch

Each valid sACN packet is dispatched by decoding the 4-byte root vector at the root vector offset and the 4-byte frame vector at the frame vector offset. Dispatch routing by frame vector:

- Streaming Data (0x00000002) — the priority byte, universe (2 bytes, little-endian), 1-byte start code, and 512-byte DMX slot data are extracted. The frame is either staged (Stream Sync active) or dispatched immediately to the frame router.
- Stream Sync (0x00000003) — the synchronization address (sync universe) and the Sync packet's own universe are checked against the per-output sacnSync config field. If the packet's universe matches the configured sync universe, all staged frames for that output are committed to the DMX buffer and Stream Sync mode exits.
- Universe Discovery (0x00000004) — the universe-list payload (a bitmap of known universes) is parsed and recorded by the sender tracker to maintain the list of active sACN sources.

Packets with an unknown or unsupported frame vector are dropped silently.

## 5. DMX Frame Routing

For Streaming Data packets, the DMX payload is extracted starting at the DMX data offset: 1 start-code byte followed by up to 512 slot bytes (513 bytes total). Frame lengths exceeding 512 slot bytes are clamped to 512 before dispatch.

The universe number on the wire is 1-based; the subsystem converts it to the 0-based port address by subtracting 1 before calling routeFrame on the frame router, so the same per-output port-address matching logic serves both protocols.

- Immediate mode (Stream Sync inactive, sacnSync == 0): the frame is dispatched directly to the frame router with the protocol discriminator set to sACN, the converted universe, the decoded priority, and the source IP. The frame is also registered with the sender tracker.
- Stream-Sync staging mode (active, sacnSync > 0): the frame is staged in the per-output sacnStaged buffer. It is committed to the frame router only when a Stream Sync packet arrives or the 500 ms sync-loss timeout fires.

The priority byte (at the sACN priority offset) is forwarded to the frame router for source-priority merge decisions.

## 6. Stream Sync

The subsystem implements the E1.13 Stream Sync state machine with two states: immediate and staged. The state is controlled per-output by the sacnSync config field.

- When sacnSync > 0, received Streaming Data packets for that output are written to the per-output sacnStaged buffer instead of being dispatched immediately, and the output enters staged mode.
- A Sync packet (frame vector 0x00000003) whose universe matches the output's sacnSync field commits all staged frames: each staged frame is dispatched to the frame router as a normal committed frame, then the stage buffer is cleared and the output returns to immediate mode.
- If no Sync packet arrives within 500 ms of the output entering staged mode (or of the last staged frame being written), a sync-loss timeout fires: all staged frames are committed and the output falls back to immediate mode automatically.

The 500 ms value is a fixed grace period, not configurable at runtime.

## 7. State Management

The subsystem is functionally stateless between ticks. It holds no persistent state machine beyond the per-output configuration read at call time. The only state that persists across ticks is:

- The per-output WiFiUDP socket handles and their multicast group memberships.
- The source CID (a 16-byte UUID), initialized once at boot and read-only thereafter.
- The per-output sacnStaged staging buffers, written by the protocol layer and committed via the Stream Sync path above.

There is no global mode flag shared across outputs. Stream Sync state is evaluated independently per output via the sacnSync config field at each packet's dispatch point.

## 8. Concurrency Model

The subsystem is single-threaded on core 0. All entry points (the drain entry point and the per-socket handler) execute within the network-reception task at priority 5 on core 0. The sACN sockets are never accessed from core 1.

The source CID is initialized during system setup, before any sockets are created. A mutex serializes CID initialization to guard against any future reordering; after initialization, the CID is read-only and the CID accessor accesses it without locking.

Cross-core data exchange for sACN frames does not exist. Staged and committed frames are both handled on core 0 within the serialized netRxTask context. The seqlock-protected DMX buffers (written via the frame router on core 0, snapshotted by core 1 on read) ensure tear-free observation by the transmit task, but that is the frame router's and DMX buffer's contract, not the sACN layer's.

## 9. Configuration Integration

The subsystem reads the following runtime configuration fields from the resolved config struct at initialization or during packet handling:

| Config Field | Usage | Apply Semantics |
|---|---|---|
| output[].enabled | Determines whether the socket is opened and the multicast group joined | Reboot-apply |
| output[].universe | Base universe; when sacnUniverse is 0, the sACN universe is derived as universe + 1 | Live-apply |
| output[].sacnUniverse | Overrides the multicast universe (0 = auto, derives from universe + 1) | Live-apply |
| output[].sacnSync | Names the sync universe for Stream Sync (0 = disabled, immediate mode) | Live-apply |

All live-apply fields (universe, sacnUniverse, sacnSync) take effect on the next received packet or next socket drain without a reboot. The enabled field requires a reboot because it gates socket creation at initialization.

## 10. Lifecycle

1. Init (setup): The CID mutex is created and the source CID is initialized from NVS or the chip MAC. Then the sACN start function joins each enabled output's multicast group on each output's sACN universe.
2. mDNS (setup): The source CID is hex-encoded and published via the mDNS TXT record so controllers can identify the source.
3. Receive (netRxTask, 2 ms tick): The drain function iterates all per-output sockets; the per-socket handler drains one output's socket, validates, and dispatches frames.
4. No deinit — sockets, multicast memberships, and the CID persist for the device lifetime. There is no shutdown path.

## 11. Error Handling

| Condition | Handling |
|---|---|
| Invalid ACN root ID prefix | Packet dropped silently |
| Packet shorter than minimum sACN size (638 bytes) | Packet dropped silently |
| Unknown or unsupported frame vector | Packet dropped silently |
| Socket not ready (recvfrom returns 0 or negative) | Drain loop exits for the current socket |
| Full receive buffer / no room | Packet effectively dropped by the bounded drain |
| Stream Sync packet for a different output | Ignored (no commit) |
| Multicast group join fails | Logged; continues with remaining sockets |
| sacnSync universe has no staged frames when Sync arrives | No-op commit (no error) |
| No output matches the packet's universe | Frame dispatched to the frame router which finds no matching output (no-op) |

## 12. Resource Allocation

- Per-output WiFiUDP socket objects — statically allocated, one per output.
- Per-output receive buffers (638 bytes) — statically allocated, reused each tick (stack-reuse pattern).
- Per-output sacnStaged staging buffers — statically allocated in the DMX buffer state segment.
- Source CID — a 16-byte static buffer, initialized once.
- CID mutex — a FreeRTOS mutex handle, created once at init.
- No heap allocation occurs after initialization. No malloc, heap_caps_malloc, or new calls are issued during steady-state operation.
- No PSRAM allocation from this subsystem.

## 13. Timing and Performance

- Network-reception task period: 2 ms (core 0, priority 5).
- Packet drain budget: capped at 4 packets per socket per tick.
- Minimum sACN packet size: 638 bytes.
- Stream Sync grace period: 500 ms sync-loss timeout before auto-commit.
- Socket mode: non-blocking.
- AsyncTCP isolation: AsyncTCP is pinned to core 0 at priority 10 with a 16 KB stack and queue depth of 128, keeping the sACN receive path on core 0 and isolating the RDM timing path on core 1.

## 14. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| core.frame-router | called by sACN | routeFrame() — dispatches committed sACN streaming frames (proto = sACN) to all matching outputs |
| core.sender-tracker | called by sACN (via frame router) | Source registration for sACN stream packets and universe discovery lists |
| core.merge-engine | called by frame router | Blending of sACN-sourced frames with other sources per output |
| core.dmx-buffer | written by Stream Sync path | sacnStaged[] staging arrays and committed buffer slots (seqlock-protected) |
| cfg.config-schema | read by sACN | output[].enabled, universe, sacnUniverse, sacnSync |
| net.sacn-pkt-queue | sACN packet queue | SPSC ring for sACN packet buffering (sibling module, same protocol domain) |
| sys.tasks | drives sACN | 2 ms netRxTask tick invokes the drain entry point |
| mDNS / system setup | consumes sACN | CID accessor for hex-encoded source CID in TXT record |
