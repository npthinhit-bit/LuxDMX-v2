# DMX UART RX — Technical Reference

Domain: drv.dmx-uart-rx

## 1. Domain Scope

This module is the **DMX512 input** path. It configures an ESP32 UART as an 8N2 @ 250 000 baud receiver and assembles incoming bytes into a 513-byte DMX frame after detecting the DMX break. It is split across two layers within the `drv` directory:

- **DMX-input framing** (`dmx_input.cpp` / `dmx_input.h`): high-level frame assembly, break-to-frame lifecycle, completion detection via a 2 ms inter-byte timeout. This path retransmits received DMX into the network protocol pipeline (converter mode).
- **UART RX primitive** (`uart_rx.h`): thin inline wrapper around the ESP-IDF UART driver (`uart_driver_install`, `uart_read_bytes`, `uart_flush_input`). This is consumed by the **RDM engine** (`src/core/rdm_engine.cpp`, `rdm_disc.cpp`, `rdm_task.cpp`) to read RDM responses on a dedicated RX-only UART.

The DMX-input framing path is owned by core 0 (called from `inputRouterPoll` in the main `loop()` or `netRxTask`). The UART-RX primitive is called from core 1 by the RDM task. The two uses share no mutable state — the framing path uses its own `g_dmxInFrame` global; the RDM path uses its own caller-owned buffers.

## 2. Layer Mapping

| Layer | Module path | Role here |
|---|---|---|
| **drv** | `src/drv/dmx_input.h`, `dmx_input.cpp` | DMX input: UART config, break detection, frame assembly |
| **drv** | `src/drv/uart_rx.h` | UART RX primitive: init, flush, blocking read |
| **cfg** | `src/cfg/config_schema.cpp` | `inputMode`, `port`, `rxPin` fields drive DMX-input init (lines 160, 162, 175) |
| **core** | `src/core/output_init.cpp` | Calls `dmxInInit` (line 86) during `outputInitAll()` |
| **core** | `src/core/input_router.cpp` | Calls `dmxInPoll` (line 21), consumes `g_dmxInFrame` (lines 27-28) |
| **core** | `src/core/rdm_engine.cpp` | Calls `uartRxInit` (line 54), `uartRxFlush` (lines 101,109), `uartRxRead` (line 116) |
| **core** | `src/core/rdm_disc.cpp` | Calls `uartRxRead` (line 27), `uartRxFlush` (line 70), `gpioDeSet` (line 28) |
| **core** | `src/core/rdm_task.cpp` | Calls `uartRxFlush` (line 60), `gpioDeSet` (line 101) |
| **sys** | `src/sys/tasks.cpp` | `dmxTxTask` (core 1) owns the RMT channel that RDM RX is coordinated with |
| **app/sys** | `src/main.cpp` | `inputRouterPoll()` called from `loop()` (line 153) |

## 3. Source Files

| File | Role |
|---|---|
| `src/drv/dmx_input.h` | `DmxInFrame` struct, `DMX_IN_BUF_SIZE` define, `g_dmxInFrame` / `g_dmxInFrameReady` externs, `dmxInInit` / `dmxInPoll` declarations. |
| `src/drv/dmx_input.cpp` | Implementation of `dmxInInit` and `dmxInPoll` — UART driver install, hardware break-detect register polling, 2 ms inter-byte timeout frame completion. |
| `src/drv/uart_rx.h` | `uartRxInit` (RX-only UART, 8N2@250k), `uartRxFlush`, `uartRxRead` — inline primitives for RDM response RX. |

The `DMX_IN_BUF_SIZE` macro is defined in `src/drv/dmx_input.h:12` as `DMX_PACKET_SIZE` (513, from `include/rdm_types.h:33`).

## 4. Data Structures

**`DmxInFrame`** — `src/drv/dmx_input.h:14-19`: the assembled DMX input frame, owned by the framing path only.

| Field | Type | Line | Description |
|---|---|---|---|
| `data` | `uint8_t[DMX_PACKET_SIZE]` (513) | 15 | Start code + 512 slots. First byte is the DMX start code. |
| `len` | `uint16_t` | 16 | Valid byte count in `data[]`. Either 2 (minimum break+1 slot) or `DMX_PACKET_SIZE` (513, full frame). |
| `ts` | `uint32_t` | 17 | `millis()` timestamp when the frame was completed. |
| `valid` | `bool` | 18 | Set `true` when a complete frame is ready; cleared by the consumer (`src/core/input_router.cpp:21`). |

**Framing-path statics** — `src/drv/dmx_input.cpp:14-19`:

| Symbol | Type | Line | Description |
|---|---|---|---|
| `g_dmxInFrame` | `DmxInFrame` | 14 | The single assembled frame buffer. Overwritten on the next completed frame. |
| `g_dmxInFrameReady` | `volatile bool` | 15 | Flag set when `g_dmxInFrame.valid` is set; consumed by `inputRouterPoll` (`src/core/input_router.cpp:21`). |
| `g_dmxInIdx` | `uint16_t` (static) | 17 | Write cursor into `g_dmxInFrame.data[]` during assembly. |
| `g_dmxInLastByteMs` | `uint32_t` (static) | 18 | `millis()` of the last received byte; drives the 2 ms timeout. |
| `g_dmxInFrameStart` | `bool` (static) | 19 | `true` once the first post-break byte is stored; gates timeout vs. live assembly. |

**No per-instance struct** for the UART-RX primitive (`uart_rx.h`); it operates on `uart_port_t` parameters passed by the RDM engine. The RDM line UART mapping is in `src/core/rdm_engine.h:23`: `static const uart_port_t RDM_LINE_UART[RDM_MAX_LINES] = { UART_NUM_2, UART_NUM_1 }`.

## 5. Concurrency

**Two independent single-threaded consumers; no shared mutable state between them.**

- **DMX-input framing path** runs on **core 0**. `dmxInPoll()` is called from `inputRouterPoll()` which is called from `main.cpp:153` (`loop()`). Its globals (`g_dmxInFrame`, `g_dmxInFrameReady`, etc.) are core-0-only — the `volatile` on `g_dmxInFrameReady` (`src/drv/dmx_input.h:22`) is a defensive marker, not a cross-core handoff.
- **UART-RX primitive** runs on **core 1** via the RDM task. `rdmReadFrame()` (`src/core/rdm_engine.cpp:112-129`) calls `uartRxRead(g_rdm.uart, ...)` (line 116) inside a `micros()`-bounded loop with a 9 ms total timeout (`RDM_RESP_TIMEOUT_MS = 9`, `rdm_engine.h:19`). `uartRxFlush` (line 101, 109) and `gpioDeSet` (line 102, 107, 135) are also core-1.
- The RDM task (priority 18, core 1) and `dmxTxTask` (priority 19, core 1) share the RMT+UART+GPIO resource cluster but never the UART-RX globals: RDM uses caller-owned stack buffers (`uint8_t rx[96]` at `rdm_engine.cpp:133`) while DMX-input uses `g_dmxInFrame`. No lock is needed.
- `g_dmxInFrameReady` is `volatile` but is only read/written on core 0 (`src/drv/dmx_input.cpp:15,76,107`, `src/core/input_router.cpp:21`). No `__sync_synchronize` barrier is used (`src/drv/dmx_input.cpp:50-113`), confirming the single-core assumption.

## 6. State Machine

**Framing path (`dmxInPoll`) — 4-state implicit FSM:**

| State | Condition (exit) | Line | Transition |
|---|---|---|---|
| `IDLE` (frameStart=false, idx=0) | UART BRK_DET raw bit set | 60 | → BREAK (reset idx=0, frameStart=false) |
| `BREAK` | First byte read after break clear | 84-90 | → ASSEMBLING (frameStart=true, idx=1) |
| `ASSEMBLING` | No new bytes for >2 ms AND idx>0 | 71-81 | → COMPLETE (valid=true, ready=true) |
| `ASSEMBLING` | idx reaches DMX_PACKET_SIZE (513) | 103-110 | → COMPLETE (len=513, valid=true, ready=true) |

After COMPLETE, the frame stays in `g_dmxInFrame` until the consumer reads it and `dmxInPoll` is called again, which resets `g_dmxInIdx=0`, `g_dmxInFrameStart=false` on the next break (`src/drv/dmx_input.cpp:67-68`).

**UART-RX primitive path — stateless:** `uartRxInit` configures once; `uartRxFlush` and `uartRxRead` are pure calls to the IDF UART driver with no retained state (`src/drv/uart_rx.h:27-35`).

## 7. Entry Points

| Entry point | First call site | Line | Caller context |
|---|---|---|---|
| `dmxInInit(int uartNum, int rxPin)` | `outputInitAll()` | `src/core/output_init.cpp:86` | One-shot per output with `inputMode != 0 && rxPin >= 0`; guarded by `cfg.outputs[i].inputMode != DMX_IN_OFF` (line 85). |
| `dmxInPoll(int uartNum)` | `inputRouterPoll()` | `src/core/input_router.cpp:21` | Called every `loop()` iteration (`main.cpp:153`), per enabled output with active input mode. |
| `uartRxInit(uart_port_t uart, int rxPin)` | `rdmRmtInit()` | `src/core/rdm_engine.cpp:54` | One-shot per RDM line during `outputInitAll` (`output_init.cpp:96`). |
| `uartRxFlush(uart_port_t uart)` | `rdmTx()`, `rdmReadResp()`, `rdmDiscBranch()` | `rdm_engine.cpp:101,109`; `rdm_disc.cpp:70`; `rdm_task.cpp:60` | Before/after each RDM TX/RX transaction on core 1. |
| `uartRxRead(uart_port_t uart, uint8_t* buf, int maxLen, int timeoutMs)` | `rdmReadFrame()` | `src/core/rdm_engine.cpp:116` | Core 1, 1 ms per read attempt, bounded by 9 ms total. Also `rdm_disc.cpp:27` (6 ms timeout). |

## 8. Data Flow

### DMX input → network (converter mode)

1. **`inputRouterPoll()`** (`src/core/input_router.cpp:13-33`) loops over all outputs, skipping any where `inputMode == DMX_IN_OFF` (line 17) or `port < 1 || port > 2` (line 18).
2. **`dmxInPoll(out.port)`** (`dmx_input.cpp:50-113`) runs the framing FSM for the UART identified by `out.port`:
   - Checks the hardware break-detect RAW bit: `REG_READ(UART_INT_RAW_REG(uart)) & UART_BRK_DET_INT_RAW` (line 60). If set, clears it (line 66) and resets frame assembly (`idx=0`, `frameStart=false`, lines 67-68).
   - If `frameStart && idx > 0 && (now - lastByteMs) > 2` (line 71): frame is complete by timeout. If `idx >= 2`, marks `g_dmxInFrame.valid=true`, `g_dmxInFrameReady=true` (lines 73-77).
   - Reads up to 64 bytes from the UART RX FIFO: `uart_read_bytes(uart, buf, 64, 0)` (line 84, zero-timeout poll).
   - On first byte after break: stores it as start code at `data[0]` (`src/drv/dmx_input.cpp:87`), sets `frameStart=true` (line 89), `idx=1` (line 88).
   - On subsequent bytes: appends to `g_dmxInFrame.data[idx++]` (lines 96-99), bounded by `DMX_PACKET_SIZE` (lines 91, 96).
   - If `idx >= DMX_PACKET_SIZE` (513) the frame is full: marks complete (lines 103-110).
   - Returns `g_dmxInFrameReady` (line 80 or 112).
3. **On frame ready**, `inputRouterPoll` branches by `inputMode` (`src/core/input_router.cpp:23-31`):
   - `DMX_IN_TO_NET` (value 1, `config_enums.h:7`): calls `updateSender()` (line 27) and `routeFrame()` (line 28) with `g_dmxInFrame.data`, `g_dmxInFrame.len`, priority 100.
   - `DMX_IN_MONITOR` (value 2): frame is left in `g_dmxInFrame` for the web UI to read (`src/core/input_router.cpp:30` comment).

### RDM response read (UART-RX primitive)

1. **`rdmTx()`** transmits the RDM request over RMT, then `gpioDeSet(g_rdm.de, 0)` sets DE/RE low (RX/listen) at `src/core/rdm_engine.cpp:107`.
2. **`rdmReadFrame()`** (`rdm_engine.cpp:112-129`) enters a `micros()`-bounded loop (9 ms = `RDM_RESP_TIMEOUT_MS`, `rdm_engine.h:19`). Each iteration calls `uartRxRead(g_rdm.uart, rx + n, rxMax - n, 1)` — a 1 ms blocking UART read (line 116). Accumulates bytes into a caller-owned `rx[]` buffer, scanning for the RDM start code `0xCC` followed by `0x01` (`RDM_SC`/`RDM_SC_SUB`).
3. Frame length is derived from `rx[sc + 2]` (the RDM message length byte, line 123); the loop exits when `n - sc >= need` (line 124) or the 9 ms budget expires.
4. **`rdmReadResp()`** (`rdm_engine.cpp:131-175`) validates the frame: checks minimum length (`n < 26` → fail, line 138), locates the start code (lines 140-142), validates message length and checksum (lines 147-152). Returns `false` on any failure.

**RDM discovery** (`rdm_disc.cpp:27`) uses `uartRxRead` with a 6 ms timeout (`RDM_DISC_TIMEOUT_MS`, `rdm_engine.h:20`) instead of the 9 ms transaction timeout.

## 9. Protocol Layout

**DMX512 frame as received** (`src/drv/dmx_input.h:14-19`, `DMX_PACKET_SIZE = 513` at `include/rdm_types.h:33`):

| Byte offset | Length | DMX / RDM meaning |
|---|---|---|
| 0 | 1 | Start code (SC). For DMX: `0x00` = general illumination. For RDM: `0xCC` = `RDM_SC`. |
| 1–512 | 512 | Slot data (8-bit values 0–255). For standard DMX: channels 1–512. |

**UART configuration** (shared by both paths, `src/drv/dmx_input.cpp:24-30`, `uart_rx.h:11-17`):

| Parameter | Value | Source |
|---|---|---|
| Baud rate | 250 000 | `dmx_input.cpp:25`, `uart_rx.h:12` |
| Data bits | 8 | `dmx_input.cpp:26`, `uart_rx.h:13` |
| Parity | Disabled (N) | `dmx_input.cpp:27`, `uart_rx.h:14` |
| Stop bits | 2 (N2) | `dmx_input.cpp:28`, `uart_rx.h:15` |
| Flow control | Disabled | `dmx_input.cpp:29`, `uart_rx.h:16` |
| RX FIFO read size | 64 bytes per poll | `dmx_input.cpp:83-84` |
| UART driver buffer | DMX_IN_BUF_SIZE + 16 (529) | `dmx_input.cpp:31` |
| UART driver buffer (RDM) | 512 | `uart_rx.h:18` |

**Break detection** — hardware register polling, no interrupt:

| Register | Line | Bits | Purpose |
|---|---|---|---|
| `UART_INT_RAW_REG(uart)` | `dmx_input.cpp:60` | `UART_BRK_DET_INT_RAW` | DMX break (>= 88 µs low) detected by hardware. |
| `UART_INT_CLR_REG(uart)` | `dmx_input.cpp:46,66` | `UART_BRK_DET_INT_CLR` | Clears the raw break-detect status. |

No interrupt is enabled — the comment at `src/drv/dmx_input.cpp:37-45` explains that "break detection is always-on hardware (no enable bit)" and that raw-register polling is race-free because the driver ISR never clears this bit.

## 10. Config Integration

This module reads configuration indirectly — its callers pass parameters derived from `Config.outputs[]`:

| Config field (`DmxOutput`) | Consumer maps it | CFG flags | Live/reboot |
|---|---|---|---|
| `inputMode` | `input_router.cpp:17` → `DMX_IN_OFF`(0)/`DMX_IN_TO_NET`(1)/`DMX_IN_MONITOR`(2) | `OENUM`, line 175 of `config_schema.cpp` | `CFG_LIVE` |
| `port` | `output_init.cpp:86` → `dmxInInit(port, rxPin)`; `input_router.cpp:21` → `dmxInPoll(port)` | `OINT`, line 160 | `CFG_REBOOT` |
| `rxPin` | `output_init.cpp:86` → `dmxInInit(port, rxPin)` | `OINT`, line 162 | `CFG_REBOOT` |
| `rtsPin` | `output_init.cpp:76` → `g_outputs[i].dePin` → `rdmRmtInit` → `gpioDeInit` | `OINT`, line 163 | `CFG_REBOOT` |
| `mode` | `output_init.cpp:64` → `resolveOutputMode()` (`include/output.h:28`): if `mode == OUTPUT_MODE_RDM_FULL` or `rtsPin >= 0`, UART RX + DE/RE GPIO are initialized | `OENUM_R`, line 171 | `CFG_REBOOT` |

The DMX-input framing path only activates when `inputMode != DMX_IN_OFF` (`src/core/output_init.cpp:85`). The UART-RX primitive only activates when `mode == OUTPUT_MODE_RDM_FULL` (line 66 → `rdmRmtInit` at line 96 → `uartRxInit` at `rdm_engine.cpp:54`). No config field is read directly inside `dmx_input.h`, `dmx_input.cpp`, or `uart_rx.h`.

## 11. Lifecycle

| Phase | Function | Call site | Notes |
|---|---|---|---|
| Init (DMX input) | `dmxInInit(uartNum, rxPin)` | `output_init.cpp:86` → `dmxInPoll` (not called during init) | Installs UART driver, configures 8N2@250k, sets pin, enables pull-up, clears stale BRK_DET status. |
| Init (RDM UART RX) | `uartRxInit(uart, rxPin)` | `rdm_engine.cpp:54` (via `rdmRmtInit` → `output_init.cpp:96`) | Installs UART driver (512-byte RX buffer), configures 8N2@250k, RX-only pin config, pulls up. |
| Runtime (DMX input) | `dmxInPoll(uartNum)` | `input_router.cpp:21` → `main.cpp:153` (`loop()`) | Called every loop iteration, no blocking delay. Polls UART RAW break bit + reads FIFO non-blocking (`uart_read_bytes(..., 0)`). |
| Runtime (RDM UART RX) | `uartRxRead(uart, buf, maxLen, timeoutMs)` | `rdm_engine.cpp:116` (`rdmReadFrame`), `rdm_disc.cpp:27` | Core 1, 1 ms per read, bounded by 9 ms (transaction) or 6 ms (discovery). |
| Runtime (RDM flush) | `uartRxFlush(uart)` | `rdm_engine.cpp:101,109`, `rdm_disc.cpp:70`, `rdm_task.cpp:60` | Called before TX and after listen-window opens, to discard stale bytes. |
| Cleanup | (none) | — | No deinit function exists for either path. UART drivers are installed once and persist. |

## 12. Error Handling

| Return | Line | Condition |
|---|---|---|
| `bool` from `dmxInInit` | 21 | Returns `false` if `rxPin < 0` (line 22) or `uartNum < 1 || uartNum > 2` (same line — only UART1/UART2 usable; UART0 is reserved for serial console). Returns `false` if `uart_driver_install` fails (line 31-32). Returns `true` on success (line 47). |
| `bool` from `dmxInPoll` | 50 | Returns `false` early if `uartNum < 1 || uartNum > 2` (line 51). Returns `g_dmxInFrameReady` (line 80 or 112) — the ready flag set during assembly. No error logging; silent failure. |
| `bool` from `uartRxInit` | `uart_rx.h:9` | Returns `false` if `rxPin < 0` (line 10) or `uart_driver_install` fails (line 18). Returns `true` otherwise (line 24). |
| `int` from `uartRxRead` | `uart_rx.h:33` | Returns `uart_read_bytes()` result directly — bytes read (0 on timeout/empty). No error wrapping. |
| `void` from `uartRxFlush` | `uart_rx.h:27` | Calls `uart_flush_input()` — returns void, no error path. |

Error handling is minimal and silent — failures return `false` or `0` with no `ESP_LOGE`/`Serial.printf` logging (unlike the RMT TX module which logs via `Serial`). The only diagnostic is the `[DMX] out%d input init failed` message emitted by the *caller* `output_init.cpp:90-93`, not by `dmxInInit` itself.

## 13. Allocation

- **UART driver RX buffers**:
  - DMX input path: `DMX_IN_BUF_SIZE + 16 = 529` bytes (`dmx_input.cpp:31`) — allocated by the IDF UART driver inside `uart_driver_install`.
  - RDM UART path: `512` bytes (`uart_rx.h:18`).
- **`g_dmxInFrame`**: global struct, 513 + 2 + 4 + 1 = 520 bytes in `.bss` (`src/drv/dmx_input.cpp:14`). Not heap-allocated.
- **Polling buffer**: `uint8_t buf[64]` stack-local in `dmxInPoll` (`src/drv/dmx_input.cpp:83`).
- **`sym` buffer**: the RMT symbol buffer (if used by the same output for RDM TX) is allocated in `rmtDmxInit` (`src/drv/dmx_rmt.h:112`), not in this module.
- No PSRAM allocations. All UART driver buffers are internal DRAM managed by the IDF.

## 14. Timing

| Constraint | Value | Source | Basis |
|---|---|---|---|
| UART baud | 250 000 | `dmx_input.cpp:25`, `uart_rx.h:12` | DMX512 / RDM line rate |
| UART data format | 8N2 | `dmx_input.cpp:26-28` | E1.11 / E1.20 |
| Inter-byte timeout | 2 ms | `dmx_input.cpp:71` | Frame-completion fallback; hardware break detect is primary. |
| RDM response read | 9 ms total | `RDM_RESP_TIMEOUT_MS`, `rdm_engine.h:19`; loop bounded at `rdm_engine.cpp:115` | E1.20 max response time (controller → responder). |
| RDM read per-attempt | 1 ms | `uartRxRead(..., 1)` at `rdm_engine.cpp:116` | Per UART read poll within the 9 ms window. |
| RDM discovery read | 6 ms | `RDM_DISC_TIMEOUT_MS`, `rdm_engine.h:20` → `rdm_disc.cpp:27` | Discovery response window. |
| UART driver timeout | 0 (polling) | `uart_read_bytes(uart, buf, 64, 0)` at `dmx_input.cpp:84` | Non-blocking FIFO drain. |
| Inter-byte timeout immunity | Hardware-timed BRK_DET | `dmx_input.cpp:37-45` | Comment: "immune to core-0 CPU load (WiFi/AsyncTCP)". |
| DMX-input poll frequency | Every `loop()` | `main.cpp:153` | Best-effort; no fixed RTOS tick. |
| UART RX on RX pin | GPIO pull-up | `dmx_input.cpp:36`, `uart_rx.h:23` | `GPIO_PULLUP_ONLY` on the RX pin. |

The 2 ms inter-byte timeout (`src/drv/dmx_input.cpp:71`) is a fallback — the primary frame-start marker is the hardware break detector, which is immune to core-0 CPU contention (`src/drv/dmx_input.cpp:37-45`).

## 15. Traceability

| Claim | Source |
|---|---|
| DMX input via UART polling with break detection | `src/drv/dmx_input.cpp:1-7` (header comment) |
| DMX break seen as 0 bits; RX FIFO flushed on break | `src/drv/dmx_input.cpp:2-4` |
| UART configured 8N2 @ 250000 baud | `src/drv/dmx_input.cpp:24-30` |
| UART1/UART2 only (UART0 reserved for console) | `src/drv/dmx_input.cpp:22` (`uartNum < 1 \|\| uartNum > 2`) |
| UART driver installed with DMX_IN_BUF_SIZE + 16 buffer | `src/drv/dmx_input.cpp:31` |
| Hardware break detection via BRK_DET raw register polling, no interrupt | `src/drv/dmx_input.cpp:37-46, 60` |
| Break clears by writing UART_INT_CLR_REG | `src/drv/dmx_input.cpp:66` |
| Frame assembly reset on break (idx=0, frameStart=false) | `src/drv/dmx_input.cpp:67-68` |
| 2 ms inter-byte timeout as frame-completion fallback | `src/drv/dmx_input.cpp:71` |
| First byte after break = start code | `src/drv/dmx_input.cpp:87` |
| Bytes bounded by DMX_PACKET_SIZE (513) | `src/drv/dmx_input.cpp:91,96` |
| Full-frame completion at idx >= DMX_PACKET_SIZE | `src/drv/dmx_input.cpp:103-110` |
| `g_dmxInFrame` global + `volatile g_dmxInFrameReady` | `src/drv/dmx_input.cpp:14-15` |
| `DmxInFrame` struct: data[513], len, ts, valid | `src/drv/dmx_input.h:12,14-19` |
| `dmxInInit` declaration | `src/drv/dmx_input.h:24` |
| `dmxInPoll` declaration | `src/drv/dmx_input.h:25` |
| UART RX primitive: 8N2 @ 250k, RX-only, 512-byte buffer | `src/drv/uart_rx.h:9-18` |
| `uartRxFlush` wraps `uart_flush_input` | `src/drv/uart_rx.h:27-29` |
| `uartRxRead` wraps `uart_read_bytes` with pdMS_TO_TICKS | `src/drv/uart_rx.h:33-35` |
| RDM line UART mapping: UART_NUM_2, UART_NUM_1 | `src/core/rdm_engine.h:23` |
| `dmxInInit` called per output in `outputInitAll` | `src/core/output_init.cpp:85-86` |
| `dmxInPoll` called per output in `inputRouterPoll` | `src/core/input_router.cpp:21` |
| `inputRouterPoll` called from `loop()` | `src/main.cpp:153` |
| `uartRxInit` called in `rdmRmtInit` | `src/core/rdm_engine.cpp:54` |
| `uartRxFlush`/`uartRxRead` called in `rdmTx`/`rdmReadFrame`/`rdmReadResp` | `src/core/rdm_engine.cpp:101-116` |
| `uartRxRead` called in `rdmDiscBranch` with 6 ms timeout | `src/core/rdm_disc.cpp:27` |
| `uartRxFlush` called in `rdmUnMuteAll` | `src/core/rdm_disc.cpp:70` |
| `uartRxFlush` called in RDM task UNMUTE_ALL branch | `src/core/rdm_task.cpp:60` |
| DMX_IN_OFF(0)/DMX_IN_TO_NET(1)/DMX_IN_MONITOR(2) enum | `include/config_enums.h:7` |
| RDM resp timeout 9 ms | `src/core/rdm_engine.h:19` |
| RDM discovery timeout 6 ms | `src/core/rdm_engine.h:20` |
| DE/RE GPIO HIGH=drive, LOW=listen | `src/drv/gpio_dir.h:2-4` |
| DE/RE GPIO init idle HIGH (keep DMX running) | `src/drv/gpio_dir.h:15` |
| DE/RE GPIO set in rdmTx (drive=1, listen=0) | `src/core/rdm_engine.cpp:102,107,135` |
| `resolveOutputMode`: rdm full if mode=1 or rtsPin>=0 | `include/output.h:28-32` |

## 16. Cross-References

| Module doc | Consumes (this module provides) | Provides to that module |
|---|---|---|
| [GPIO DIR](./drv-gpio-dir.md) | — | DE/RE GPIO control (`gpioDeInit`, `gpioDeSet`) used alongside UART RX in `rdm_engine.cpp:53,102,107,135`, `rdm_disc.cpp:28,70`, `rdm_task.cpp:101` |
| [DMX RMT TX](./drv-dmx-rmt-tx.md) | — | RMT channel that DE/RE GPIO gates; RDM TX uses `rmtDmxEncode`/`rmt_transmit` (`rdm_engine.cpp:103-105`) |
| [Output Init](./core-output-init.md) | `dmxInInit` (output_init.cpp:86), `uartRxInit` (via rdmRmtInit → rdm_engine.cpp:54) | Output mode resolution, UART/UART-RX pin assignment from config |
| [RDM Engine](./core-rdm-engine.md) | `uartRxInit` (rdm_engine.cpp:54), `uartRxFlush` (rdm_engine.cpp:101,109), `uartRxRead` (rdm_engine.cpp:116) | UART RX for RDM response reading |
| [RDM Disc](./core-rdm-discovery.md) | `uartRxRead` (rdm_disc.cpp:27), `uartRxFlush` (rdm_disc.cpp:70), `gpioDeSet` (rdm_disc.cpp:28) | — |
| [RDM Task](./core-rdm-task.md) | `uartRxFlush` (rdm_task.cpp:60), `gpioDeSet` (rdm_task.cpp:101) | — |
| [Input Router](./core-input-router.md) | `g_dmxInFrame` (input_router.cpp:11,27), `dmxInPoll` (input_router.cpp:21) | Consumes assembled DMX frames for network retransmission |
| [Include Headers](./include-headers.md) | `dmx_output_t` (include/output.h), `config_enums.h` DMX input modes, `rdm_types.h` DMX_PACKET_SIZE | — |
| [Sys Tasks](./sys-tasks.md) | — | Runs on same core 1 as `dmxTxTask`; RDM task (priority 18) shares RMT+UART+GPIO cluster |

## 17. Limitations

- **No deinit path**: neither `dmx_input.cpp` nor `uart_rx.h` provides a UART teardown function. Once `uart_driver_install` is called during `outputInitAll`, the UART instance persists until reset (`src/drv/dmx_input.cpp:31`, `src/drv/uart_rx.h:18`). A re-init is not supported.
- **Silent error handling**: `dmxInInit` returns `false` on failure but does not log (`src/drv/dmx_input.cpp:22-51`); the only diagnostic is the caller's `[DMX] out%d input init failed` message (`src/core/output_init.cpp:90-93`). `dmxInPoll` never logs at all.
- **64-byte poll buffer truncates burst reads**: `dmxInPoll` reads at most 64 bytes per call (`src/drv/dmx_input.cpp:83-84`); at 250 kbaud (44 µs/slot) a 513-slot frame arrives in ~23 ms, so the UART FIFO can accumulate more than 64 bytes between polls if `loop()` is delayed (e.g., by WiFi events). This is self-correcting (next poll drains the rest) but a single poll can only advance assembly by 64 slots.
- **UART NUM_MAX sentinel**: `uartRxInit` is called with `UART_NUM_MAX` as a default in `rdmRmtInit` (`src/core/rdm_engine.cpp:45-49`); `rdmRmtInit` checks `if (uart == UART_NUM_MAX) uart = RDM_LINE_UART[idx]` (`src/core/rdm_engine.h:23`) before calling `uartRxInit`. If that resolution is ever skipped, `uartRxInit` would pass `UART_NUM_MAX` to `uart_driver_install`, which is invalid.
- **`DMX_IN_BUF_SIZE` alias**: `src/drv/dmx_input.h:12` defines `DMX_IN_BUF_SIZE` as `DMX_PACKET_SIZE` — a redundant alias of the same constant defined in `include/rdm_types.h:33`. If `DMX_PACKET_SIZE` changes, both stay in sync only because the alias re-references it, but the duplication is a maintenance hazard.
- **No parity/framing error handling**: the UART is configured for 8N2, but framing errors (e.g., a partial break misinterpreted as a byte) are not checked or logged (`src/drv/dmx_input.cpp:50-113`).
- **Break detection polling not rate-limited**: `dmxInPoll` reads `UART_INT_RAW_REG` on every call (`src/drv/dmx_input.cpp:60`). While hardware-timed, the register read cost is paid at every `loop()` iteration regardless of whether a break occurred.

## 18. Open Questions

- Not determinable from the inspected source code — why `DMX_IN_BUF_SIZE` is a separate alias of `DMX_PACKET_SIZE` (`dmx_input.h:12`) rather than using the canonical constant directly.
- Not determinable from the inspected source code — whether the 64-byte poll read buffer (`dmx_input.cpp:83`) is sufficient under worst-case WiFi coexistence latency, or whether a larger FIFO drain size was measured as unnecessary.
- Not determinable from the inspected source code — whether the `volatile` qualifier on `g_dmxInFrameReady` (`dmx_input.h:22`) is required (it is core-0-only per the analysis in the Concurrency section; no cross-core handoff exists, so `volatile` is defensive or vestigial).
- Not determinable from the inspected source code — the recovery path when `uart_driver_install` fails for the DMX input UART (`dmx_input.cpp:31`): the caller logs a message but the output's input mode is effectively dead for the boot session with no retry.
- Not determinable from the inspected source code — whether the UART driver instance for DMX input is ever reused for RDM on the same UART number, or whether DMX input and RDM always use distinct UARTs (UART1 vs UART2 per `RDM_LINE_UART`).

## 19. Testing

No test coverage for this domain. No native or unit-test files exist in `test/native/` or `test/unit-test/` that reference `dmxInInit`, `dmxInPoll`, `uartRxInit`, `uartRxFlush`, `uartRxRead`, or `DmxInFrame`. The `rdm_types_test` (`test/native/rdm_types_test.cpp:51`) only verifies `DMX_PACKET_SIZE == 513`, which this module consumes via `DMX_IN_BUF_SIZE` (`src/drv/dmx_input.h:12`). UART register polling (`UART_INT_RAW_REG`, `UART_BRK_DET_INT_RAW`) and IDF UART driver calls (`src/drv/dmx_input.cpp:84`, `src/drv/uart_rx.h:18,23,34`) have no shim-compatible test path.

## 20. History

- **Issue #64** (core separation): DMX input polling was moved to core 0 / main `loop()`, separate from the core-1 RDM TX/RX path, to keep the RMT timing path uncontended (`src/drv/dmx_input.h:4-6` comment).
- **Hardware break detection migration**: the original design comment at `src/drv/dmx_input.cpp:1-7` describes a 2 ms `millis()` inter-byte timeout as the frame-boundary marker. The current implementation adds hardware `BRK_DET` register polling as the primary frame-start marker (`src/drv/dmx_input.cpp:55-69`), with the 2 ms timeout retained as a fallback (line 71 comment). This change makes break detection immune to core-0 CPU load.
- **RDM architecture (dispatch vs low-level)**: `rdm_engine.h:2` documents the split — RDM request TX goes out over RMT, responses come on a "RX-only UART, DE/RE is a GPIO" (line 2-5). The `uart_rx.h` primitive provides this RX-only path (`src/drv/uart_rx.h:1-5`).
- **RDM task offload**: `rdmReadFrame`/`rdmReadResp`/`rdmDiscBranch` were moved to the dedicated RDM task (core 1, priority 18, `src/core/rdm_task.h:12-14`) so they never block the DMX TX task. The UART-RX primitive calls (`uartRxRead`, `uartRxFlush`) execute on that task thread (`src/core/rdm_task.cpp:14`, `rdm_engine.cpp:96`).
