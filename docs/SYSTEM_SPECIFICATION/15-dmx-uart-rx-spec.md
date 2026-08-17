# DMX UART RX Driver Specification

Domain: drv.dmx-uart-rx

## 1. Module Overview

The DMX UART RX driver is the DMX512 input path. It configures an ESP32 UART as an 8N2 receiver at 250 000 baud and assembles incoming bytes into a 513-byte DMX frame after detecting the DMX break via hardware break-detection register polling. The module is split across two layers within the driver directory:

- **DMX-input framing:** High-level frame assembly. Detects the hardware-reported break, assembles the start code and 512 data slots, and completes the frame on either a full 513 bytes or a 2 ms inter-byte timeout. This path feeds received DMX into the network protocol pipeline (converter mode).
- **UART RX primitive:** A thin inline wrapper around the ESP-IDF UART driver (`uart_driver_install`, `uart_read_bytes`, `uart_flush_input`). This is consumed by the RDM engine to read RDM responses on a dedicated RX-only UART.

The DMX-input framing path runs on core 0 (called from `inputRouterPoll` in the main loop). The UART-RX primitive runs on core 1 via the RDM task. The two uses share no mutable state — the framing path uses its own global frame buffer; the RDM path uses caller-owned stack buffers. UART0 is reserved for the serial console; only UART1 and UART2 are available for DMX input and RDM.

## 2. External Interfaces

### DMX-Input Framing API

| Function | Signature | Behavior |
|---|---|---|
| `dmxInInit` | `(int uartNum, int rxPin)` | Installs the UART driver (8N2 at 250 kbaud), configures the RX pin with pull-up, and clears any stale break-detect status. Returns `bool`. |
| `dmxInPoll` | `(int uartNum)` | Runs the framing finite-state machine for one poll cycle. Polls the hardware break-detect register, drains up to 64 bytes from the RX FIFO, and assembles them into a frame. Returns `bool` (true if a frame is ready). |

### UART-RX Primitive API

| Function | Signature | Behavior |
|---|---|---|
| `uartRxInit` | `(uart_port_t uart, int rxPin)` | RX-only UART init (8N2 at 250 kbaud, 512-byte RX buffer). Returns `bool`. |
| `uartRxFlush` | `(uart_port_t uart)` | Discards all pending RX FIFO data. Returns `void`. |
| `uartRxRead` | `(uart_port_t uart, uint8_t* buf, int maxLen, int timeoutMs)` | Blocking read with millisecond timeout. Returns byte count (`int`). |

### Data Structures

| Struct | Fields | Description |
|---|---|---|
| `DmxInFrame` | `data[513]`, `len`, `ts`, `valid` | Assembled DMX input frame. `data[0]` is the start code; `len` is 2 or 513. `valid` set by producer, cleared by consumer. |
| `RdmLine.uart` | `uart_port_t` | UART port for RDM response RX. Mapped: RDM line index 0 -> UART2, index 1 -> UART1. |

### UART Configuration (shared by both paths)

| Parameter | Value |
|---|---|
| Baud rate | 250 000 |
| Data bits | 8 |
| Parity | None |
| Stop bits | 2 |
| Flow control | Disabled |
| RX pin | GPIO with pull-up enabled |

### Hardware Break Detection

| Register | Bit | Purpose |
|---|---|---|
| `UART_INT_RAW_REG` | `UART_BRK_DET_INT_RAW` | DMX break (>= 88 us low) detected by hardware. |
| `UART_INT_CLR_REG` | `UART_BRK_DET_INT_CLR` | Clears the raw break-detect status. |

Break detection is hardware-timed and uses raw-register polling with no interrupt enabled. This makes frame-start detection immune to core-0 CPU contention (WiFi, AsyncTCP).

## 3. State Machine

### DMX-Input Framing FSM (4 states)

| State | Entry condition | Exit condition | Action |
|---|---|---|---|
| `IDLE` | `frameStart = false`, `idx = 0` | Hardware break detected (RAW bit set) | Reset index to 0, `frameStart = false`, clear break status |
| `BREAK` | Break detected | First byte read after break clear | Store byte as start code at `data[0]`, set `frameStart = true`, `idx = 1` |
| `ASSEMBLING` | After first byte, `frameStart = true` | No new bytes for >2 ms with `idx > 0` | Mark frame complete (`valid = true`, `ready = true`, `len = idx`) |
| `ASSEMBLING` | Receiving bytes | `idx` reaches 513 | Mark frame complete (`valid = true`, `ready = true`, `len = 513`) |

After completion, the frame stays in the global buffer until the consumer reads it and `dmxInPoll` is called again on the next break, which resets the assembly state.

### UART-RX Primitive

Stateless. `uartRxInit` configures once; `uartRxFlush` and `uartRxRead` are pure calls to the IDF UART driver with no retained state.

## 4. Data Flow

### DMX Input to Network (converter mode)

1. **Input router poll:** `inputRouterPoll` iterates all outputs, skipping any where the input mode is off or the port is invalid (1 or 2 only).
2. **Framing FSM:** `dmxInPoll` runs the 4-state machine for the UART identified by the output's port:
   - Checks the hardware break-detect RAW bit. If set, clears it and resets frame assembly (index = 0, frameStart = false).
   - If `frameStart` is true, `idx > 0`, and no bytes for >2 ms: marks the frame complete by timeout.
   - Reads up to 64 bytes from the UART RX FIFO (non-blocking, zero timeout).
   - Stores the first post-break byte as the start code, subsequent bytes as slot data, bounded by 513 bytes.
   - If `idx >= 513`, marks the frame complete by count.
   - Returns the ready flag.
3. **Mode dispatch:** When a frame is ready, the input router branches by `inputMode`:
   - `DMX_IN_TO_NET` (1): calls `updateSender` to track the source, then `routeFrame` to bridge the frame into the network protocol pipeline with priority 100.
   - `DMX_IN_MONITOR` (2): leaves the frame in the global buffer for the web UI to read.

### RDM Response Read (UART-RX primitive)

1. **RDM TX:** The RDM engine transmits the request over RMT, sets DE/RE low (listen), delays 90 us for transceiver turnaround, and flushes the RX UART.
2. **Response poll:** `rdmReadFrame` enters a microsecond-timed loop with a 9 ms total budget. Each iteration calls `uartRxRead` with a 1 ms timeout. Bytes are accumulated into a caller-owned buffer, scanning for the RDM start code pattern (0xCC followed by 0x01).
3. **Frame completion:** The in-packet length byte determines when the response is complete. The loop exits when the expected byte count is reached or the 9 ms budget expires.
4. **Validation:** `rdmReadResp` checks minimum length (26 bytes), locates the start-code pair, validates the 8-bit additive checksum, and extracts source UID, response type, message count, PID, and parameter data length.

RDM discovery uses a 6 ms timeout variant of `uartRxRead` instead of the 9 ms transaction timeout.

## 5. Configuration Integration

This module reads configuration indirectly — its callers pass parameters derived from the output config:

| Config field | Consumer maps it | CFG flag | Live/Reboot |
|---|---|---|---|
| `inputMode` | Input router (DMX_IN_OFF / TO_NET / MONITOR) | CFG_LIVE | Live |
| `port` | Output init -> `dmxInInit(port, rxPin)`; input router -> `dmxInPoll(port)` | CFG_REBOOT | Reboot |
| `rxPin` | Output init -> `dmxInInit(port, rxPin)` | CFG_REBOOT | Reboot |
| `rtsPin` | Output init -> DE/RE GPIO; mode resolution | CFG_REBOOT | Reboot |
| `mode` | Output init -> `resolveOutputMode()` (RDM-full if mode=1 or rtsPin>=0) | CFG_REBOOT | Reboot |

The DMX-input framing path activates only when `inputMode` is not off. The UART-RX primitive activates only when the output mode is RDM-full. No config field is read directly inside the driver source files.

## 6. Lifecycle

| Phase | Function | Notes |
|---|---|---|
| Init (DMX input) | `dmxInInit(uartNum, rxPin)` | One-shot per output during output init. Installs UART driver, configures 8N2 at 250 kbaud, sets RX pin with pull-up, clears stale break status. Only called when `inputMode != OFF` and `rxPin >= 0`. |
| Init (RDM UART RX) | `uartRxInit(uart, rxPin)` | One-shot per RDM line. Installs UART driver (512-byte RX buffer), configures 8N2 at 250 kbaud, RX-only pin, pulls up. |
| Runtime (DMX input) | `dmxInPoll(uartNum)` | Called every main loop iteration, per enabled output with active input mode. Non-blocking — polls break register and drains FIFO with zero timeout. |
| Runtime (RDM UART RX) | `uartRxRead(uart, buf, maxLen, timeoutMs)` | Core 1, 1 ms per read attempt, bounded by 9 ms (transaction) or 6 ms (discovery). |
| Runtime (RDM flush) | `uartRxFlush(uart)` | Called before TX and after listen window opens, to discard stale bytes. Core 1. |
| Cleanup | None | No deinit function exists for either path. UART drivers are installed once and persist until reset. |

## 7. Error Handling

| Function | Return type | Failure condition | Behavior |
|---|---|---|---|
| `dmxInInit` | `bool` | `rxPin < 0` or `uartNum` out of range (1-2) | Returns `false`, no error logging |
| `dmxInInit` | `bool` | `uart_driver_install` failure | Returns `false`, no error logging |
| `dmxInPoll` | `bool` | `uartNum` out of range | Returns `false` early |
| `dmxInPoll` | `bool` | No frame ready | Returns `false` |
| `uartRxInit` | `bool` | `rxPin < 0` or `uart_driver_install` failure | Returns `false` |
| `uartRxRead` | `int` | UART timeout or empty FIFO | Returns 0 |
| `uartRxFlush` | `void` | N/A (always succeeds) | Flushes input FIFO |

Error handling is minimal and silent. Failures return `false` or `0` with no `ESP_LOGE` or `Serial.println` logging from the driver itself. The only diagnostic is the caller's log message when DMX input init fails during output initialization.

## 8. Timing Constraints

| Constraint | Value | Basis |
|---|---|---|
| UART baud rate | 250 000 | DMX512 / RDM line rate |
| UART data format | 8N2 | E1.11 / E1.20 |
| Inter-byte timeout | 2 ms | Frame-completion fallback; hardware break detect is primary |
| RDM response read | 9 ms total | `RDM_RESP_TIMEOUT_MS`; E1.20 max response time |
| RDM read per attempt | 1 ms | Per UART read poll within the 9 ms window |
| RDM discovery read | 6 ms | `RDM_DISC_TIMEOUT_MS` |
| UART RX FIFO poll | Zero timeout (non-blocking) | In `dmxInPoll` |
| Break detection | Hardware-timed | Immune to core-0 CPU load (WiFi, AsyncTCP) |
| RX pin | GPIO pull-up | Electrical idle-high for DMX mark |
| DMX input poll frequency | Every main loop iteration | Best-effort, no fixed RTOS tick |

The 2 ms inter-byte timeout is a fallback — the primary frame-start marker is the hardware break detector, which is immune to core-0 CPU contention. A full 513-slot frame arrives in ~23 ms at 250 kbaud, so the 64-byte per-poll read can drain the FIFO in multiple passes if the main loop is delayed.

## 9. Memory & Allocation Model

- **UART driver RX buffers (allocated by IDF):** DMX input path uses 529 bytes (513 + 16); RDM path uses 512 bytes. These are managed internally by the IDF UART driver and cannot be freed by this module.
- **`g_dmxInFrame` global:** 520 bytes in BSS (513-byte data array + len + timestamp + valid flag). Not heap-allocated.
- **Polling buffer:** 64 bytes on the stack in `dmxInPoll`.
- **RDM read buffer:** 96 bytes on the stack in `rdmReadFrame`, caller-owned.
- No PSRAM allocations. No heap allocation occurs in this module beyond what the IDF UART driver performs internally.

## 10. Safety Considerations

- **Hardware break detection:** Break detection uses the UART hardware break-detect register, which is always-on with no interrupt enable bit. This is immune to core-0 CPU contention from WiFi or AsyncTCP — the original cause of frame-boundary misses was a software `millis()` inter-byte timeout, which could be delayed by network interrupts.
- **Core isolation:** The DMX-input framing path runs on core 0; the RDM UART-RX primitive runs on core 1 via the dedicated RDM task. They share no mutable state and use distinct UART ports (UART1/UART2), so no contention is possible.
- **No mid-frame UART release:** The RX-only UART is dedicated to RDM response reading and is never released mid-frame. The RMT TX peripheral (used for DMX output and RDM requests) runs uninterrrupted between RDM operations because the DE/RE GPIO stays HIGH (drive) during DMX output.
- **DE/RE GPIO coordination:** The DE/RE pin is toggled around RDM transactions by the GPIO direction module. The 90 us turn-round delay after TX ensures the transceiver is in listen mode before the UART RX is armed.
- **Frame size bounding:** Byte assembly is bounded by `DMX_PACKET_SIZE` (513), preventing buffer overrun regardless of UART FIFO depth.

## 11. Cross-Module Dependencies

| Module | Provides to this module | Consumes from this module |
|---|---|---|
| GPIO DIR | DE/RE GPIO control for RDM transceiver direction | — |
| DMX RMT TX | Shared RMT TX channel for RDM requests | — |
| Output Init | UART port and RX pin from config | `dmxInInit`, `uartRxInit` |
| Input Router | Calls `dmxInPoll`, consumes `g_dmxInFrame` | `g_dmxInFrame`, `dmxInPoll` |
| RDM Engine | Calls `uartRxInit`, `uartRxFlush`, `uartRxRead` | UART RX primitive for response reading |
| RDM Discovery | Calls `uartRxRead` (6 ms), `uartRxFlush`, `gpioDeSet` | UART RX for discovery responses |
| RDM Task | Calls `uartRxFlush`, `gpioDeSet` | UART flush primitives |
| Config Engine | `inputMode`, `port`, `rxPin`, `rtsPin`, `mode` fields | Input mode resolution |
| Sys Tasks | `dmxTxTask` (core 1, priority 19) owns the shared RMT/UART/GPIO cluster | — |

This module depends on the ESP-IDF UART driver API and the `DmxInFrame` / `dmx_output_t` struct definitions. It has no dependencies on the net, app, or sys task layers.

## 12. Testing Verification

No host-native or hardware test coverage exists for this module. No native or unit test files reference any of the functions (`dmxInInit`, `dmxInPoll`, `uartRxInit`, `uartRxFlush`, `uartRxRead`) or the `DmxInFrame` struct. The driver is hardware-bound with no shim-compatible test path — UART register polling and IDF UART driver calls require real ESP-IDF hardware context. The only related test is `rdm_types_test` which verifies `DMX_PACKET_SIZE == 513`, a constant consumed by this module but defined in the RDM types header.

## 13. Open Questions

- Why `DMX_IN_BUF_SIZE` is a separate alias of `DMX_PACKET_SIZE` rather than using the canonical constant directly.
- Whether the 64-byte poll read buffer is sufficient under worst-case WiFi coexistence latency, or whether a larger FIFO drain size was measured as unnecessary.
- Whether the `volatile` qualifier on the frame-ready flag is required (it is core-0-only with no cross-core handoff, so it is defensive or vestigial).
- The recovery path when `uart_driver_install` fails for the DMX input UART: the caller logs a message but the output's input mode is effectively dead for the boot session with no retry.
- Whether the UART driver instance for DMX input is ever reused for RDM on the same UART number, or whether DMX input and RDM always use distinct UARTs.

## 14. History

- **Issue #64 (core separation):** DMX input polling was moved to core 0 / main loop, separate from the core-1 RDM TX/RX path, to keep the RMT timing path uncontended.
- **Hardware break detection migration:** The original design used a 2 ms `millis()` inter-byte timeout as the frame-boundary marker. The current implementation adds hardware `BRK_DET` register polling as the primary frame-start marker, with the 2 ms timeout retained as a fallback. This change makes break detection immune to core-0 CPU load.
- **RDM architecture decision:** The RDM engine documents its transport design as "RDM requests go out over RMT (same channel as DMX), responses come back on a RX-only UART, DE/RE is a GPIO." The UART RX primitive provides this RX-only path. The architectural decision record states that the RDM engine uses low-level primitives directly — no task dispatches to itself.
- **RDM task offload:** `rdmReadFrame`/`rdmReadResp`/`rdmDiscBranch` were moved to the dedicated RDM task (core 1, priority 18) so they never block the DMX TX task. The UART-RX primitive calls execute on that task thread.
