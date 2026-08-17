# Input Router - System Specification

Domain: core.input_router

## 1. Module Overview

Owns the DMX-input-to-network bridge polling loop. It reads completed DMX frames from
enabled UART RX ports and routes them into the live sender table and frame router so they
can be retransmitted over Art-Net or sACN (converter mode).

The module:

- Iterates every configured output and, for each one whose input mode is active, polls the
  associated UART for a completed DMX frame.
- Dispatches a ready frame into the network pipeline as a local source (Art-Net protocol tag,
  default priority), registering it in the sender table before routing it to mapped outputs.
- Exposes a single stateless entry point invoked from the core-0 main loop. There is no
  fixed RTOS tick; dispatch timing follows main-loop iteration.

It delegates nothing. UART RX framing (hardware break detection and byte assembly) belongs to
the UART RX driver. Source tracking and frame routing belong to the sender tracker and frame
router. This module is the glue that connects a received DMX frame to the network merge path.

## 2. External Interfaces

### Owned entry point

| Interface | Caller | Trigger | Core |
|---|---|---|---|
| inputRouterPoll | Main-loop (loop) | After WebSocket push and dirty-flag processing, every loop iteration | Core 0 |

### Called downstream

| Dependency | Called by this module | Arguments (typical) |
|---|---|---|
| dmxInPoll | On every active output | uartNum (1 or 2) |
| updateSender | When a frame is ready and mode is TO_NET | ip=0, proto=0, portAddr, priority=100, data, len |
| routeFrame | When a frame is ready and mode is TO_NET | portAddr, data, len, ip=0, proto=0, priority=100 |
| portAddress | Per active output | DmxOutput descriptor -> 15-bit port address |

### Inputs (read-only)

- cfg.outputs[i] -- enabled flag, inputMode, port (UART number) for each output.
- outReady[i] -- per-output readiness gate (true after RMT/output init succeeded).
- g_dmxInFrame -- the most recently completed DMX frame (data, length, timestamp, valid).

### Outputs (written)

- Sender table entry via updateSender.
- DMX buffer staging via routeFrame -> frame router -> merge engine -> seqlock buffer.

There are no return values and no output parameters exposed to callers beyond these side
effects.

## 3. State Machine

No state machine. inputRouterPoll is a stateless request/response poller: each call is
independent and re-evaluates the readiness of every output from scratch. There are no
transition states and no retained per-output state inside this module.

## 4. Data Flow

1. **Loop dispatch (core 0)**: loop() calls inputRouterPoll() after serial console polling,
   dirty-flag processing, WebSocket push, and RDM queue draining.
2. **Output iteration**: loop over all MAX_OUTPUTS outputs.
3. **Readiness gate**: for each output, skip if disabled, if inputMode is OFF, if the port is
   out of the supported range (1-2 only; port 1 -> UART1, port 2 -> UART2; UART0 reserved for
   the serial console), or if outReady[i] is false (RMT/output init did not succeed).
4. **UART poll**: call dmxInPoll(port). This polls the UART hardware break-detect register
   (the primary frame-start marker, immune to core-0 CPU load) and, falling back to a 2 ms
   inter-byte timeout, assembles the start code + 512 slots into g_dmxInFrame. Returns true
   when a complete frame is present.
5. **Mode dispatch**: when dmxInPoll returns true:
   - DMX_IN_TO_NET: compute the 15-bit port address, register the source via updateSender
     (ip=0, proto=0, priority=100), then route the frame via routeFrame into the network
     merge pipeline.
   - DMX_IN_MONITOR: leave the frame in g_dmxInFrame for the web UI to read; no network
     action.
   - DMX_IN_OFF: already skipped at the readiness gate.

## 5. Configuration Integration

| Config field | Layer | Live / Reboot | How used |
|---|---|---|---|
| outputs[i].enabled | Config struct | Reboot | Skips outputs that are disabled (read for every output each call). |
| outputs[i].inputMode | Config struct | Live | Selects OFF / TO_NET / MONITOR; changed via web UI or serial console and applied on the next poll. |
| outputs[i].port | Config struct | Reboot | UART number for DMX input. Read for every active output; validated as 1 or 2. |

Write: none. The input router never persists or modifies config.

The inputMode field is live and applies on the next poll without a reboot. The port and
enabled fields are reboot-bound because the UART driver is initialized once at boot during
output bring-up.

## 6. Lifecycle

- **Init**: none in this module. It depends on dmxInInit having been called per output UART
  during output bring-up (outputInitAll, invoked from setup before the main loop). The
  outReady array is populated there and gates this module at runtime.
- **Runtime**: inputRouterPoll() is called from loop() every iteration on core 0. Effective
  period is best-effort and depends on main-loop iteration time, not a fixed tick.
- **Shutdown**: none. UART drivers are installed once and persist until reset; no cleanup
  hook exists.

## 7. Error Handling

| Condition | Behavior |
|---|---|
| Output disabled, inputMode OFF, invalid port (not 1-2), or not ready | continue to next output; no logging. |
| dmxInPoll returns false | no frame ready; move to the next output. |
| portAddress computes an address | always succeeds for a well-formed output descriptor. |
| updateSender / routeFrame | return void; invalid universes or unmapped senders are dropped silently. |

All error handling is silent. inputRouterPoll returns void. Failures are inferred by the
absence of network activity, not by an error code.

## 8. Timing Constraints

| Constraint | Value | Notes |
|---|---|---|
| Poll period | Best-effort main-loop iteration | Not a fixed RTOS tick. |
| UART poll | Zero-timeout FIFO drain | uart_read_bytes with zero tick; up to 64 bytes per call. |
| Break detection | Hardware-timed | Primary frame-start marker; immune to core-0 CPU load. |
| Inter-byte timeout | 2 ms | Frame-completion fallback when no new bytes arrive. |
| Frame size | up to 513 bytes (start code + 512 slots) | DMX_PACKET_SIZE. |
| Per-frame dispatch cost | ~20 us on core 0 at 240 MHz | O(16) sender scan + O(MAX_OUTPUTS) route frame. |
| Frame arrival time | ~23 ms at 250 kbps | A full 513-slot frame; may span multiple 64-byte poll drains. |

## 9. Memory and Allocation Model

- **g_dmxInFrame**: static DRAM, not heap. Holds the start code + 512 slots (513 bytes) plus
  frame metadata (length, timestamp, validity flag); the assembled struct is approximately
  520 bytes. It is written by the UART driver and read by this module in the same core-0
  context.
- **outReady[]**: static DRAM; one byte per output.
- **cfg.outputs**: part of the config struct, statically placed by the config engine.
- **64-byte UART poll buffer**: stack-allocated, transient per dmxInPoll call.
- No heap allocation. No PSRAM. No malloc / new within this module.

## 10. Safety Considerations

- **Hardware break detection**: the DMX break is detected by the UART hardware break-detect
  register, which is always-on with no interrupt enable. This is immune to core-0 CPU
  contention from WiFi or AsyncTCP -- the failure mode this fixes was a software millis()
  inter-byte timeout that could be delayed by network interrupts.
- **Core isolation**: the input router and UART framing path run on core 0; the RDM UART-RX
  primitive runs on core 1 via the dedicated RDM task. They share no mutable state and use
  distinct UART ports, so no contention is possible.
- **Port range check**: only ports 1 and 2 (UART1, UART2) are accepted; UART0 is reserved for
  the serial console and cannot be used for DMX input.
- **Frame-size bounding**: byte assembly is bounded by DMX_PACKET_SIZE (513), preventing
  buffer overrun regardless of UART FIFO depth.
- **Readiness gate**: outReady[i] must be true before a UART is polled, preventing reads on a
  UART whose driver was never installed or whose RMT init failed.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| drv.dmx-uart-rx | used by | dmxInPoll (frame assembly + hardware break detection), g_dmxInFrame (consumed frame). |
| core.output-init | used by | outReady readiness gate; output bring-up calls dmxInInit. |
| core.frame-router | calls onward | routeFrame, portAddress -- routes the received frame to mapped outputs and triggers the merge. |
| core.sender-tracker | calls onward | updateSender -- registers the DMX-in source as a sender. |
| core.merge-engine | downstream | mergeOutput is triggered by routeFrame after the sender update. |
| core.dmx-buffer | downstream | the seqlock-protected output buffer that routeFrame writes to. |
| cfg (config_schema) | read | outputs[i].enabled, inputMode, port. |
| include.config-enums | used by | DMX_IN_OFF / DMX_IN_TO_NET / DMX_IN_MONITOR constants. |
| include.rdm-types | used by | DMX_PACKET_SIZE (513) consumed by the UART driver. |
| net.artnet / net.sacn | downstream | the network protocol pipeline that ultimately retransmits the frame. |
| sys.tasks | invokes this module | loop() runs on core 0, separate from the 1 ms dmxTxTask on core 1. |

## 12. Testing Verification

No dedicated unit test or native test exists for the input router. No native or unit-test file
references inputRouterPoll, dmxInPoll from this context, or the DMX_IN_* dispatch logic.

What IS covered by existing tests:
- config_test -- config resolution generically, including the inputMode field, but not the
  router dispatch logic.
- merge_test -- the merge engine; no DMX-in or input-router coverage.
- seqlock_test, rdm_types_test -- no input-router coverage.
- rdm_types_test verifies DMX_PACKET_SIZE == 513, a constant consumed by this module's UART
  driver dependency, but the struct logic itself is not tested off-target.

DMX-in framing is validated on hardware only: dmxInPoll requires a live UART and a real DMX
signal; the native shim layer does not provide a UART shim.

## 13. Open Questions

1. Whether DMX_IN_MONITOR frames are pushed to the WebSocket in any path outside the
   inspected files. The frame is left in g_dmxInFrame, but no WebSocket dispatch of monitor
   data is visible in this module.
2. Whether the hard-coded priority 100 is intentional or should reference the DEFAULT_PRIORITY
   constant instead. It matches that constant but is passed literally.
3. Whether g_dmxInFrameReady (declared alongside g_dmxInFrame) is consumed by any code path
   outside the inspected files. This module relies on dmxInPoll's boolean return rather than
   the ready flag.
4. Whether inputRouterPoll is the only caller of dmxInPoll, or whether RDM response RX also
   polls the same UART port on core 1. The two uses share no mutable state but may share a
   UART if configured on the same port index.

## 14. History

- **Initial design**: a single inputRouterPoll() poller for DMX-in -> Art-Net/sACN
  retransmission, invoked from the main loop on core 0.
- **DMX_IN_MONITOR mode**: added to support a monitor use case where the web UI can read raw
  DMX input without retransmitting it onto the network.
- **Hardware break detection migration**: the original design used a 2 ms millis() inter-byte
  timeout as the frame-boundary marker. The current implementation adds hardware break-detect
  register polling as the primary frame-start marker, with the 2 ms timeout retained as a
  fallback. This makes break detection immune to core-0 CPU load from WiFi and AsyncTCP.
- **Priority hard-coding**: priority 100 is passed explicitly for DMX-in sources, matching the
  DEFAULT_PRIORITY constant but not referenced through it.