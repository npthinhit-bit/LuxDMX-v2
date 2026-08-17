# RDM Engine - System Specification

## 1. Module Overview

**Module ID:** core.rdm_engine
**Domain:** E1.20 RDM controller transport layer
**Layer:** core (drives drv hardware, dispatches to core-1 RDM task)

Owns the low-level E1.20 transport for RDM controller operations: line (output) registration, request packet assembly, RMT-based transmission, UART-based response reception, and response parsing. It is the stateless transport beneath the typed-PID wrappers and the discovery primitives.

Key architectural decisions:

- **RMT TX** for outgoing request packets -- the RMT peripheral clocks the entire symbol stream in hardware, so the CPU is never preempted mid-frame by network DMA contention on core 0.
- **RX-only UART** for incoming responses -- the DMX output line (RMT) runs uninterrupted between RDM transactions; the receive UART is never released mid-frame.
- **DE/RE GPIO direction control** -- high (drive) during TX, low (listen) during RX; switched per transaction.
- **Core 1 exclusive ownership** -- all transport primitives execute on the dedicated RDM task (priority 18), so no mutex protects the RMT channel or UART.

### Capacity

| Constant | Value | Meaning |
|---|---|---|
| RDM_MAX_LINES | 2 | Maximum RDM-capable outputs |
| RDM_MAX_RETRIES | 3 | Retry attempts per transaction |
| RDM_RESP_TIMEOUT_MS | 9 | Response receive timeout |
| RDM_DISC_TIMEOUT_MS | 6 | Discovery read timeout |

## 2. External Interfaces

### Entry Points (declared by this module, implemented in the task layer)

| Function | Direction | Purpose |
|---|---|---|
| rdmTransaction(...) | Called by typed wrappers and Art-Net bridge | Blocking dispatch to the core-1 RDM task; returns parsed response |
| rdmRmtRawRelay(...) | Called by async relay path | Non-blocking enqueue to the core-1 RDM task with line-index selection |
| rdmOp*() family | Declared here | Typed PID wrappers (implemented in the typed layer) |

### Entry Points (owned by this module)

| Function | Caller | Trigger | Core |
|---|---|---|---|
| rdmRmtInit(idx, rmt, dePin, rxPin, uart) | Output bring-up | Per RDM-capable output | Core 0 (init) |
| rdmRmtSelect(lineIdx) | RDM task | Before each transaction | Core 1 |
| rdmBuild(buf, dstUid, cmdClass, pid, data, pdl) | RDM task / discovery | Per request | Core 1 |
| rdmTx(buf, len) | RDM task / discovery | Per transmit | Core 1 |
| rdmReadResp(&ack) | RDM task / discovery | After transmit | Core 1 |
| rdmSavePoll() | Boot loop | Dirty-flag flush | Core 0 (deferred) |

### Input (read-only)

- Destination UID, command class, PID, and parameter data -- from callers building a request.
- g_rdm state: registered lines, active RMT handle, DE/RE pin, RX UART, transaction counter, statistics counters.
- WiFi MAC address -- read on first line registration to derive the controller UID.

### Output (written)

| Target | What | Core |
|---|---|---|
| RMT peripheral | Encoded request symbols | Core 1 |
| DE/RE GPIO | Direction state (high=drive, low=listen) | Core 1 |
| UART RX buffer (flushed) | Before/after TX | Core 1 |
| rdm_ack_t struct | Parsed response metadata (src UID, PID, response type, message count, PDL) | Core 1 |
| g_rdm counters (sent, recv, timestamps) | Statistics | Core 1 |
| NVS (dmxgw namespace) | rdmCount, sent, recv counters | Core 0 (poll flush) |

## 3. State Machine

No explicit state machine -- the engine is a stateless transport. Each transaction follows an implicit micro-sequence:

- **IDLE**: DE/RE pin low (listen); UART flushed.
- **DRIVE**: DE/RE pin set high; RMT transmits the assembled request.
- **WAIT**: RMT transmit completion waited (up to 60 ms); 90 us post-TX silence.
- **LISTEN**: DE/RE pin set low; UART RX collects the response for up to 9 ms.
- **PARSE**: Start-code scan, length/checksum validation, field extraction into rdm_ack_t.
- Return to **IDLE**.

## 4. Data Flow

1. **Controller UID initialization**: On the first registered output, the controller UID is derived from the ESP32 WiFi station MAC -- bytes 2-5 of the MAC become the 32-bit device ID, with manufacturer ID hard-wired to 0x4C58 ("LX").

2. **Line registration**: rdmRmtInit() stores the RMT handle, DE/RE GPIO pin, RX pin, and UART port into the line table. On the first line, the controller UID is initialized and line 0 is selected as active.

3. **Request build**: rdmBuild() assembles a standard E1.20 packet: start code (0xCC) -> sub-start code (0x01) -> length byte -> 6-byte destination UID -> 6-byte source/controller UID -> transaction counter (incrementing) -> message count -> sub-device/reserved fields -> command class -> 2-byte PID -> 1-byte PDL -> parameter data -> 2-byte additive checksum.

4. **Transmit**: rdmTx() sets DE high (drive), encodes the frame into RMT symbols, fires the RMT peripheral, blocks until transmission completes (up to 60 ms), sets DE low (listen), delays 90 us to let the responder begin, and flushes the RX UART.

5. **Response receive**: The UART is polled for up to 9 ms. The byte stream is scanned for the 0xCC + 0x01 start-code pattern; the in-packet length byte determines when the frame is complete.

6. **Response parse**: rdmReadResp() validates a minimum of 26 bytes, locates the start-code pair, checks the 8-bit additive checksum over the declared message length, and extracts the source UID, response type (ACK/NACK/ACK_TIMER/INVALID/NONE), responder's queued message count, echoed PID, and parameter data length. If checksum or length validation fails, the function returns false with a diagnostic failure label.

7. **NVS persistence**: Statistics (sent, recv, rdmCount) are written to the "dmxgw" NVS namespace when flushed by the boot-loop dirty flag.

## 5. Configuration Integration

| Field | CFG flag | Default | How used |
|---|---|---|---|
| rdmMaxDev | CFG_REBOOT | board template | Caps the discovery result count (applied by the task wrapper, not this module) |

The engine itself does not read Config fields directly. The MAX_OUTPUTS constant is available via the config schema header for callers.

## 6. Lifecycle

- **Init (per-line)**: rdmRmtInit() is called during output bring-up. On the first line, the controller UID is initialized from the WiFi MAC and line 0 is selected as active. The RMT channel, DE/RE GPIO, and RX UART are claimed for the lifetime of that line.
- **Select (per-transaction)**: rdmRmtSelect() copies the chosen line's active RMT handle, DE/RE pin, RX pin, and UART into the global active-transport state. Guarded against out-of-range line index.
- **Poll (idle)**: A dirty flag for NVS counter persistence is flushed in the boot loop.
- **Shutdown**: None. The RMT channel and UART are never released -- DMX output continues uninterrupted between RDM operations.

## 7. Error Handling

| Function | Return | Failure condition |
|---|---|---|
| rdmRmtInit | int | Line table full (RDM_MAX_LINES reached) -- returns -1 |
| rdmRmtSelect | void | Line index out of range -- early return |
| rdmTx | void | Active RMT handle is null -- early return |
| rdmReadFrame | int | Timeout expires with no start-code match -- returns 0 or partial |
| rdmReadResp | bool | Packet shorter than 26 bytes ("short"); no start-code pair ("noSC"); length mismatch ("len"); checksum mismatch ("ck") |

No diagnostic logging is emitted; failures propagate via bool/int returns and a diagnostic failure-label string pointer that callers may inspect but the engine itself does not log.

## 8. Timing Constraints

| Constraint | Value | Core |
|---|---|---|
| Response receive timeout | 9 ms | Core 1 |
| Discovery read timeout | 6 ms | Core 1 |
| Post-TX DE-low delay | 90 us | Core 1 |
| RMT transmit wait (blocking) | 60 ms | Core 1 |
| UART RX read timeout per poll | 1 ms | Core 1 |
| Transaction counter | 8-bit wrapping | Core 1 |
| Identify duration | 1500 ms | Core 1 |

The 90 us post-TX delay ensures the responder has time to begin transmitting before the RX UART is flushed and armed. The 9 ms response timeout covers the worst-case responder processing window. All timing is enforced on core 1, isolating the real-time DMX TX path on core 1 from core-0 network latency.

## 9. Memory and Allocation Model

- **g_rdm state**: Single statically allocated instance in DRAM, 64-bit aligned.
- **rx buffer**: Stack-allocated 96-byte buffer used during response reception.
- **build buffer**: Caller-provided 64-byte buffer for request assembly.
- **RMT symbol buffer**: DRAM + DMA-capable, allocated in the RMT driver.
- **Identify state**: Static DRAM (channel value, millis deadline).

No PSRAM or heap allocation occurs within this module.

## 10. Safety Considerations

- **DMX output continuity**: The DE/RE GPIO is the only direction-control mechanism; it is set high (drive) only during a request TX and immediately returned to low (listen). The RMT peripheral -- which carries the live DMX stream -- is never released. This guarantees DMX transmission is uninterrupted between RDM transactions.
- **UART never mid-frame released**: The RX-only UART is flushed before TX and re-armed after the 90 us post-TX delay, ensuring no partial-frame corruption when a responder begins mid-byte.
- **Checksum validation**: Every response is validated with an 8-bit additive checksum over the declared message length before any field is extracted, preventing misparsed data from propagating to callers.
- **Core-1 isolation**: All transport primitives execute on the dedicated RDM task (priority 18). The shared RMT peripheral is never accessed from two cores simultaneously, eliminating races on the DMX/RDM signal path.
- **Transaction counter**: 8-bit wrapping counter prevents infinite-response misinterpretation from a stuck responder.
- **Minimal failure impact**: rdmReadResp returning false terminates the transaction; the higher-layer retry loop handles retransmission without corrupting DMX output.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| drv.dmx-rmt-tx | used by | RMT transmit handle, symbol encoding, rmt_transmit, rmt_tx_wait_all_done |
| drv.dmx-uart-rx | used by | UART RX init, read, flush |
| drv.gpio-dir | used by | DE/RE GPIO set (drive/listen) |
| core.rdm-task | calls rdmBuild/rdmTx/rdmReadResp/rdmReadFrame; implements rdmTransaction/rdmRmtRawRelay | Core-1 task dispatch for blocking and async paths |
| core.rdm-typed | calls rdmTransaction | Typed PID wrappers |
| core.rdm-discovery | calls rdmBuild/rdmTx/rdmReadResp/uartRxRead/gpioDeSet | DISC_UNIQUE_BRANCH, DISC_MUTE, DISC_UN_MUTE |
| net.art-rdm-resp-queue | write | SPSC response ring for async Art-Net RDM replies |
| net.artnet-bridge | calls rdmRmtRawRelayEnqueue | Art-Net RDM packet relay |
| cfg (config_schema) | read | MAX_OUTPUTS constant; rdmMaxDev (by task wrapper) |
| include.rdm-types | used by | rdm_uid_t, rdm_ack_t, command-class enum, PID constants |
| sys.tasks | schedules | RDM task (core 1, priority 18) |

## 12. Testing Verification

| Test Case | File | Validates |
|---|---|---|
| UID struct, constants, enums | rdm_types_test / unit-test/test_rdm_types | rdm_uid_t layout, RDM_SC, PID constants, command-class, response-type, sensor enums, DMX_PACKET_SIZE, RDM_ASCII_SIZE_MAX |

**Untested**: rdmBuild, rdmTx, rdmReadResp, rdmReadFrame, rdmRmtInit, rdmRmtSelect, and the NVS poll persistence have no host-native or Unity unit-test coverage. Transport correctness is validated only on hardware with live RDM responders.

## 13. Open Questions

1. Whether the one-byte field-offset difference between the response parser and the request builder has caused real-world interoperability issues with specific responder models.
2. The full set of callers that set rdmMaxDev and whether this limit is user-configurable via the web UI.
3. The origin of the queued WebSocket RDM command processor and how it interacts with the task dispatch queue.

## 14. History

- **RMT-TX + RX-only UART**: Adopted to fix core-0 network DMA contention corrupting DMX breaks during heavy WiFi traffic.
- **Task dispatch extraction**: rdmTransaction and rdmRmtRawRelay moved from the engine into the dedicated core-1 RDM task (priority 18) so blocking RDM requests never stall the DMX TX task (priority 19).
- **Transport isolation**: The engine retains only the stateless transport primitives; all blocking orchestration moved to the task layer.
- **Types forked from esp_dmx**: rdm_types.h declares all E1.20 types directly from the standard rather than depending on the esp_dmx library.