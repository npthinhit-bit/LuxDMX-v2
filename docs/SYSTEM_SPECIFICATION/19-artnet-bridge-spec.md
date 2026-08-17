# ArtNet Bridge — System Specification

Domain: net.artnet-bridge

## 1. Module Overview

The ArtNet Bridge is the control-plane opcode processor for the Art-Net protocol. It handles
all non-DMX-data opcodes arriving on the Art-Net UDP port: Poll, Address, IPProg, TodRequest,
RDM, and Sync. The bridge translates these opcodes into firmware actions: generating ArtPollReply
responses, applying address and IP-programming changes, relaying RDM requests asynchronously to
the core-1 RDM task, and committing ArtSync-staged DMX frames.

The bridge operates entirely on core 0 within the network-reception task. It owns no hardware;
it delegates all RDM I/O to the RDM task on core 1. The RDM relay path is non-blocking: the
bridge enqueues a request — including a destination output line index — and returns immediately,
while the actual RDM transaction (transmit via RMT, receive via UART, response generation) runs
on core 1 without stalling the network task.

**Consumes:** Art-Net control opcodes from the Art-Net protocol handler.
**Delegates to:** RDM task (RDM request relay), Config Engine (address/IP configuration),
DMX Buffer (ArtSync staged commits via seqlock).

## 2. External Interfaces

### 2.1 Dispatch Entry Point

| Interface | Caller | Purpose |
|---|---|---|
| bridgeDispatch(opcode, packet, length, sourceIp) | Art-Net protocol handler | Top-level dispatch for all non-DMX opcodes |

### 2.2 Opcode Handlers

| Handler | Opcode | Behavior |
|---|---|---|
| handlePoll | 0x2000 | Increments poll counter; generates and transmits a 240-byte ArtPollReply |
| handleAddress | 0x6000 | Applies ArtAddress commands: 'A' sets universe, 'N' sets net switch, 'S' sets subnet; 'U' commits and persists configuration |
| handleIpProg | 0xF800 | Marks configuration dirty for persistence if IP programming is enabled; ignores requests from non-local subnets |
| handleTodRequest | 0x8000 | No-op (not implemented) |
| handleRdm | 0x8300 | Extracts RDM payload from byte offset 18 (maximum 256 bytes); resolves target output line index; enqueues to core-1 RDM task non-blocking |
| handleSync | (handled by protocol handler, not bridge) | Sets immediate mode; commits all staged ArtSync frames to active DMX buffers |

### 2.3 ArtPollReply Fields

The 240-byte ArtPollReply response contains:

| Off | Field | Description |
|---|---|---|
| 0-7 | ID | 8-byte "Art-Net" identifier (null-terminated) |
| 8-9 | Opcode | 0x2100 (PollReply, little-endian) |
| 10 | Protocol version high | 14 |
| 11 | Protocol version low | 0 |
| 12 | Answers | 1 (device answers polls) |
| 13 | Addresses | 0 |
| 14 | Bind output port | Number of configured outputs |
| 15 | Bind input port | 0 |
| 16-17 | Node type | 0x01 (dynamic) / 0x00 |
| 18-21 | ESTA manufacturer code | Manufacturer identifier |
| 22-39 | Short name | "LuxDMX V2" padded with spaces |
| 40-73 | Long name | "LuxDMX V2 Art-Net/sACN to DMX Gateway" padded with spaces |
| 74-137 | Node report | Status string padded with spaces |
| 148-151 | Local IP | Current node IP address |
| 152-155 | Subnet mask | Current subnet mask |
| 156-159 | Gateway | Current gateway IP |
| 168-171 | Port types | 0x80 (DMX) per port |
| 172-175 | Port statuses | 0x80 if enabled, 0x00 otherwise, per port |
| 176-181 | Node MAC | WiFi station MAC address |

### 2.4 ArtRdm Request Layout

| Offset | Field |
|---|---|
| 0-7 | ID ("Art-Net" prefix) |
| 8-9 | Opcode 0x8300 (little-endian) |
| 10-11 | Protocol version (14) |
| 12 | ArtNet version |
| 13 | Firmware version |
| 14 | Flags |
| 15 | RDM command count |
| 16 | Transaction number |
| 17 | FAE source |
| 18+ | RDM packet (start code + RDM frame) |

The RDM payload begins at byte offset 18 (the RDM start code).

### 2.5 ArtRdm Response Path

Completed RDM responses arrive on a cross-core ring buffer (produced by core 1, consumed by
core 0). On each 2 ms tick, the bridge drains pending responses and transmits them as ArtRdm
reply packets (opcode 0x8300) via UDP back to the originating controller.

## 3. State Machine

The bridge maintains two staging state variables across dispatch cycles:

| Variable | States | Transition |
|---|---|---|
| Sync mode | Immediate / Staged | Set to immediate on ArtSync Sync receipt, which commits all staged frames |
| Timecode validity | Fresh / Stale | Set fresh on ArtTimeCode receipt with valid frame length; cleared by timeout |

No persistent opcode state machine exists; each opcode handler is invoked independently per
packet within a single dispatch cycle.

## 4. Data Flow

### 4.1 ArtPoll → ArtPollReply

1. The Art-Net protocol handler receives an ArtPoll opcode (0x2000).
2. The handler dispatches to the bridge via bridgeDispatch.
3. The bridge increments its poll counter and generates an ArtPollReply containing node identity,
   IP configuration, MAC address, port count, and per-port status.
4. The reply is transmitted via UDP back to the requesting controller.

### 4.2 ArtRdm → Core-1 RDM Task → Core-0 Response

1. The Art-Net protocol handler receives an ArtRdm opcode (0x8300).
2. The bridge extracts the RDM payload (starting at byte offset 18, maximum 256 bytes).
3. The bridge resolves the target output's line index from the configured RDM output table
   and enqueues the request to the core-1 RDM task queue via a non-blocking call.
4. The core-1 RDM task selects the target output's RMT channel and DE/RE GPIO, transmits
   the RDM request via RMT, then switches to receive mode on the dedicated RDM UART.
5. The completed response is pushed onto the cross-core response ring.
6. The bridge drains the response ring and transmits the ArtRdm reply (opcode 0x8300) via
   UDP back to the controller.

### 4.3 ArtSync → Staged Commit

1. The Art-Net protocol handler receives an ArtSync opcode (0x5200).
2. The handler sets sync mode to immediate and commits all staged ArtSync frames to the active
   DMX buffers behind a seqlock write bracket.
3. On the next 1 ms DMX transmit tick, the system snapshot reads the newly-committed buffers
   via the seqlock and transmits them.

### 4.4 ArtAddress → Configuration Commit

1. The Art-Net protocol handler receives an ArtAddress opcode (0x6000).
2. The bridge applies per-command changes: 'A' sets the universe, 'N' sets the net switch,
   'S' sets the subnet, per output port.
3. Command 'U' triggers configuration commit and NVS persistence, applying the staged address
   changes.

## 5. Configuration Integration

| Config Field | Apply Semantics | Usage in Bridge |
|---|---|---|
| output[].universe | Live | Set via ArtAddress command 'A' |
| output[].net | Reboot | Set via ArtAddress command 'N' |
| output[].subnet | Live | Set via ArtAddress command 'S' |
| artnetRdm | Live | Gate for RDM relay — when disabled, ArtRdm requests are not relayed to core 1 |
| ipProg | Live | Gate for ArtIpProg handling — when disabled, IP programming requests are ignored |
| useEthernet | Reboot | Not directly consumed by the bridge; affects which network interface is used for reply transmission |

ArtAddress command 'U' triggers the configuration engine to commit staged changes and persist
them to NVS. The protocol-selection field (sACN-only mode) suppresses ArtDMX handling at the
protocol handler level, not within the bridge itself.

## 6. Lifecycle

1. **Init:** During system bring-up, the bridge state structure is initialized alongside the
   Art-Net socket, the parsed-packet ring, and the RDM response ring. The socket is bound to
   port 6454 with SO_REUSEADDR and SO_BROADCAST.
2. **Dispatch:** Invoked every 2 ms tick by the network-reception task, which pulls packets
   from the parsed-packet queue and dispatches them by opcode via bridgeDispatch.
3. **Response drain:** RDM responses are drained from the cross-core response ring every
   2 ms tick and transmitted via UDP.
4. **No explicit deinit.**

## 7. Error Handling

| Condition | Handling |
|---|---|
| ArtRdm packet shorter than 24 bytes | Returns early; no enqueue, no relay |
| ArtNet socket not ready (invalid descriptor) | Returns early; no transmission attempted |
| ArtIpProg from non-local subnet | Ignored and logged |
| ArtRdm relay when artnetRdm disabled | Request is not enqueued; no action |
| ArtIpProg when ipProg disabled | Request is ignored; no config mark-dirty |
| Unknown opcode | Falls through to a no-op default |
| Empty or malformed RDM response on drain | Skipped; no transmission attempted |

## 8. Timing Constraints

- The network-reception task period is 2 ms; all bridge processing must complete within this
  budget.
- The receive budget is bounded to 8 packets per 2 ms tick to prevent network bursts from
  starving sACN processing, DNS, and the web server.
- The RDM relay path is non-blocking: the core-0 caller returns immediately after enqueueing.
  The RDM transaction runs on core 1 without blocking core 0.
- ArtSync commits are immediate; the staged frames become visible to the 1 ms DMX transmit
  tick on the next buffer snapshot.

## 9. Memory and Allocation Model

Fully static. The 240-byte ArtPollReply buffer is stack-allocated per call to handlePoll.
The ArtRdm response reply buffer (516 bytes) is stack-allocated per drained response. No heap
allocation occurs in any bridge code path. The bridge state structure is a single static
instance with no dynamic sizing.

## 10. Safety Considerations

- **Cross-core hardware isolation:** The bridge never directly accesses RMT, UART, or DE/RE
  GPIO hardware. All RDM I/O is delegated to the core-1 RDM task, eliminating races on the
  shared signal cluster that also drives live DMX output.
- **Non-blocking relay contract:** The RDM request payload is copied into the queue-owned buffer
  before the bridge returns, so the caller's packet buffer is never referenced during the
  multi-millisecond transaction on core 1.
- **Line-selection race guard:** The destination line index is resolved on core 0 and passed
  through the queue. Line selection on core 1 is guarded by validity checks on the relay
  request length and a non-negative line index, preventing two back-to-back ArtNet RDM packets
  targeting different outputs from interleaving.
- **Socket validity check:** The ArtNet socket descriptor is validated before any transmission;
  invalid sockets cause responses to be skipped without error escalation.
- **No DMX output interruption:** The RDM relay executes on core 1 alongside the DMX transmit
  task; the RMT channel carrying the live DMX stream is never released during RDM activity.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| net.artnet-protocol | calls bridge | Dispatches non-DMX opcodes to bridgeDispatch |
| core.rdm-task | writes to bridge's queue | Receives RDM relay requests and processes them on core 1 |
| net.art-rdm-resp-queue | core 0 reads, core 1 writes | Cross-core RDM response ring |
| cfg.config-engine | bridge reads/writes config | artnetRdm, ipProg, per-output universe/net/subnet; config commit on 'U' |
| core.dmx-buffer | bridge writes via seqlock | ArtSync staged frame commits |
| sys.tasks | schedules | Drives the 2 ms network-reception task containing the bridge |
| drv.dmx-rmt-tx | used transitively via rdm-task | RMT transmit for RDM requests |
| drv.dmx-uart-rx | used transitively via rdm-task | UART receive for RDM responses |
| drv.gpio-dir | used transitively via rdm-task | DE/RE GPIO direction for RDM transactions |

## 12. Testing Verification

No dedicated unit test covers the bridge's control-opcode handlers. The bridge is exercised
only through integration testing of the Art-Net receive path on hardware. No host-native test
covers ArtPollReply generation, ArtAddress parsing, IPProg handling, or the RDM relay path.

**Untested paths:**
- ArtPollReply 240-byte response assembly and UDP transmission.
- ArtAddress command parsing ('A', 'N', 'S', 'U', 'T').
- ArtIpProg source-subnet validation and config dirty marking.
- ArtRdm payload extraction and line-index resolution.
- The non-blocking enqueue contract for core-1 relay.
- ArtSync staged-commit to seqlock-protected DMX buffers.

## 13. Open Questions

1. Where sync mode is set to staged (the ArtSync enable path) — the transition to staged mode
   is not exercised within the bridge itself.
2. Whether handleTodRequest has a planned implementation (currently a stub).
3. Whether the ESTA manufacturer code is officially registered (currently a placeholder value).
4. Whether the timecode-validity timeout that clears the fresh state is enforced within the
   bridge or by the protocol handler.

## 14. History

No recorded changes.
