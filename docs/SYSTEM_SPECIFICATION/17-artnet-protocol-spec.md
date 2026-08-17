# Art-Net Protocol Subsystem — System Specification

## 1. Scope and Purpose

This document specifies the Art-Net (E1.19) protocol subsystem as a black box. It owns the Art-Net UDP socket on port 6454, receiving packets, dispatching them by opcode, staging DMX frames for ArtSync-deferred commits, routing immediate DMX data into the merge engine, and transmitting ArtRdm replies back to controllers. Control-opcode handling (Poll, Address, IPProg, RDM, TodRequest, Sync) is delegated to the ArtNet Bridge subsystem. It is consumed exclusively by the network-reception task on core 0.

## 2. System Context

The Art-Net subsystem sits at the network layer. It receives parsed-packet rings from the Art-Packet Queue and cross-core RDM reply rings from the ArtRdm Response Queue. It calls down into the core layer (frame router for DMX routing, DMX buffer for ArtSync staging, sender tracker for source registration, scene engine for timecode and trigger events) and reads runtime configuration from the config engine. The system task scheduler drives the receive-poll, dispatch, and response-drain entry points every 2 ms tick on core 0.

## 3. Packet Reception

The subsystem receives Art-Net packets via a non-blocking UDP socket on port 6454, created at initialization. Each 2 ms tick, the receive loop reads at most 8 packets into a fixed 640-byte stack buffer. The socket is configured with `SO_REUSEADDR` and `SO_BROADCAST`. Each received packet is validated against the 8-byte ID string `"Art-Net\0"` and capped at the maximum Art-Net packet size. Valid packets are pushed into the lock-free parsed-packet ring; if the ring is full, the packet is dropped (back-pressure). The receive loop terminates early if the socket is not ready or if `recvfrom` returns no data.

## 4. Packet Dispatch

After reception, each queued packet is dispatched by decoding the 2-byte opcode at byte offset 8 (little-endian). Dispatch routing by opcode:

- **ArtDMX (0x5000)** — parsed for universe, length, sequence, physical port, priority, and 512-channel data; routed to the frame router for immediate output or staged for ArtSync.
- **ArtSync (0x5300)** — commits all staged ArtSync frames, resets to immediate mode.
- **Poll (0x2000), Address (0x6000), IPProg (0xF800), TodRequest (0x8000), Rdm (0x8300)** — forwarded to the ArtNet Bridge for control handling.
- **Timecode (0x9700)** — SMPTE timecode type, hour, minute, second, and frame fields parsed; stored and forwarded to the scene engine for trigger evaluation.
- **Trigger (0x9900)** — key and subkey fields extracted; a key of 0xFF causes the subkey to be used directly as a scene index.
- **NZS (nonzero start code, 0x5800)** — start code at byte 60; routed to the nonzero-start-code frame router.

## 5. DMX Frame Routing

For ArtDMX opcodes, the universe address is read at bytes 14-15, the data length at bytes 16-17 (big-endian), sequence at byte 18, physical port at byte 19, and priority at byte 59 (defaulting to 100 if the packet is shorter than 60 bytes).

- **Immediate mode** (default): the frame is routed directly to the frame router for immediate merge-buffer insertion and sender tracking. The protocol field in config, if set to sACN-only mode, causes ArtDMX opcodes to be skipped entirely.
- **ArtSync staging mode**: frames received while sync mode is active are written to the staged DMX buffer for deferred commit. The priority byte and universe determine which output and buffer slot receives the staged data.

## 6. ArtSync and Timecode

The subsystem maintains an ArtSync staging state machine with two states: immediate (0) and staged (1). Receiving an ArtSync opcode commits all staged frames to the active DMX buffers, updates sender tracking, and returns to immediate mode.

If no ArtSync opcode is received within 1000 ms while in staged mode, a timeout fires: staged frames are committed and the system falls back to immediate mode automatically.

Timecode output is broadcast at a configurable frame rate (default 25 fps, minimum 40 ms between sends). The last received timecode is stored and made available for scene-engine evaluation on each Timecode receive.

## 7. ArtRdm Response Path

ArtRdm replies originate from core 1 (the RDM task) and are produced into a lock-free single-producer, single-consumer ring buffer. On each tick, the subsystem drains completed ArtRdm responses from this ring. Each response is transmitted via the UDP socket back to the originating controller's IP address and port. Responses that are empty or malformed are skipped. The socket descriptor must be valid for transmission to proceed.

## 8. State Management

The subsystem maintains a single static state structure with the following fields and semantics:

- Socket descriptor (valid/invalid sentinel at -1)
- Ready flag (true when socket and queues are initialized)
- Local node IP (network byte order) and MAC address
- ArtRdm enabled flag (mirrors config)
- ArtPoll request counter
- Background-queue severity policy (4 = disabled; changes flagged for NVS persistence)
- Background-queue dirty flag (cross-task read)
- Config dirty flag (set when configuration changes arrive via ArtAddress)
- Sync mode (immediate vs. staged)
- Sync timestamp for timeout tracking
- SMPTE timecode (type, hour, minute, second, frame) and validity/send flags
- Timecode output type, frame rate, and last-send timestamp

## 9. Concurrency Model

The subsystem is single-threaded on core 0, executed by the network-reception task at priority 5. The state structure is statically allocated and accessed only from this task. Volatile qualifiers on specific dirty and status flags exist to support cross-task reads from the WebSocket frame builder (also on core 0 loop), not for cross-core access. Cross-core data exchange for ArtRdm responses uses the lock-free SPSC ring: the RDM task on core 1 is the producer; the network-reception task on core 0 is the consumer via the response-drain entry point.

## 10. Configuration Integration

The subsystem reads the following runtime configuration fields at initialization or during packet handling:

- ArtNet RDM enable (reboot-apply)
- Timecode send, type, and frame rate (reboot-apply)
- DSCP marking (reboot-apply, applied to socket TOS)
- Per-output universe and port address (live-apply during dispatch)
- Protocol selection (sACN-only mode suppresses ArtDMX handling)

## 11. Lifecycle

Initialization occurs during the network-protocol init phase of system setup: the parsed-packet queue and ArtRdm response ring are initialized, the UDP socket is bound, DSCP TOS is applied, and the ready flag is set. The poll-to-dispatch-to-drain sequence runs every 2 ms tick on the network-reception task for the entire runtime. There is no explicit shutdown; the socket is released implicitly by the OS at task teardown.

## 12. Error Handling

- Socket creation failure: logged, ready flag stays false, all poll operations early-return.
- `recvfrom` returning zero or negative: the receive loop exits for the current tick.
- Packets shorter than the 12-byte minimum or opcode-specific minimums: silently skipped.
- Full packet queue on push: packet is dropped (non-blocking, back-pressure).
- Empty or malformed ArtRdm responses: skipped, no transmission attempted.
- Invalid socket descriptor during response drain: responses are skipped.
- ArtSync commit on disabled or stale outputs: skipped silently.

## 13. Resource Allocation

- The ArtNetState structure is statically allocated in the data segment — no heap.
- The 640-byte receive buffer is a static local, reused each tick (stack-reuse pattern) — no heap.
- The UDP socket is kernel-managed; the file descriptor is stored in the state structure.
- The ArtRdm response reply buffer is stack-allocated per drained packet.
- The timecode transmit buffer is stack-allocated per send.
- No PSRAM allocation. No FreeRTOS heap usage from this subsystem.

## 14. Timing and Performance

- The network-reception task period is 2 ms.
- The packet receive budget is capped at 8 packets per 2 ms tick.
- The ArtSync timeout is 1000 ms.
- The ArtSync fallback on timeout commits staged frames and returns to immediate mode.
- Timecode send rate is minimum 1000 / timecodeFps ms between sends (40 ms default at 25 fps).
- The UDP socket operates in non-blocking mode.
- AsyncTCP is pinned to core 0 at priority 10 with a 16 KB stack and queue depth of 128, isolating the RDM timing path on core 1.
