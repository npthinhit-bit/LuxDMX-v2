# RDM Engine — Technical Reference

Domain: core.rdm_engine

## 1. Domain Scope

The RDM Engine is the E1.20 transport layer for the RDM controller. It owns:

- **Line management** — registering RMT TX channels, DE/RE GPIO pins, and RX UART ports for up to `RDM_MAX_LINES` outputs, and switching the active line at `rdm_engine.cpp:36-43,60`.
- **Request framing** — assembling a standard RDM packet from destination UID, command class, PID, and parameter data at `rdm_engine.cpp:73-92`.
- **Transmit** — driving the RMT peripheral for the request and flipping the DE/RE GPIO at `rdm_engine.cpp:96-110`.
- **Response receive** — reading the RX-only UART with a bounded timeout, scanning for the start-code pattern, validating length and checksum, and unpacking the `rdm_ack_t` at `rdm_engine.cpp:112-175`.

It delegates the following to sibling modules:

- **Transaction orchestration and task dispatch** — `rdmTransaction()` and `rdmRmtRawRelay()` are declared here (`rdm_engine.h:69-72`) but implemented in [core-rdm-task](./core-rdm-task.md) (`rdm_task.cpp:304-351`), where they dispatch to the dedicated RDM task on core 1.
- **Typed PID wrappers** — the `rdmOp*()` family is declared here (`rdm_engine.h:76-112`) but implemented in [core-rdm-typed](./core-rdm-typed.md) (`rdm_typed.cpp`).
- **Discovery primitives** — `rdmRmtDiscover()` is declared in [core-rdm-discovery](./core-rdm-discovery.md) (`rdm_disc.h:23`) and implemented in `rdm_task.cpp:320`; the low-level branch/mute primitives live in `rdm_disc.cpp`.

It is consumed by: the RDM task loop ([core-rdm-task](./core-rdm-task.md)), the typed wrappers ([core-rdm-typed](./core-rdm-typed.md)), the discovery code ([core-rdm-discovery](./core-rdm-discovery.md)), the crash-guard / output init path (`output_init.cpp`), and the Art-Net RDM bridge (`artnet_bridge.cpp:149`).

## 2. Layer Mapping

| Layer | Module path | Role |
|---|---|---|
| **drv** | `src/drv/dmx_rmt.h`, `src/drv/uart_rx.h`, `src/drv/gpio_dir.h` | Hardware primitives: RMT TX channel, RX-only UART, DE/RE GPIO — all called by the RDM engine. |
| **cfg** | `include/config_schema.h` | `Config` struct and `MAX_OUTPUTS` — included via `rdm_engine.h:10`; `cfg.rdmMaxDev` consumed by `rdm_task.cpp:323`. |
| **core** | `src/core/rdm_engine.cpp`, `rdm_engine.h`, `include/rdm_types.h` | **This module** — RMT-TX + UART-RX transport, packet build, response parse. |
| **net** | `src/net/art_rdm_resp_queue.h` | SPSC ring that receives completed ArtRdm replies from the RDM task at `rdm_task.cpp:121`. |
| **sys** | `src/sys/tasks.h` | `dmxTxTask` (core 1, prio 19) and the separate `rdmTaskLoop` (core 1, prio 18) that call this engine. |

## 3. Source Files

| File | Path | Role |
|---|---|---|
| `rdm_engine.h` | `src/core/rdm_engine.h` | Public API, constants, `RdmState`/`RdmLine` structs, inline accessors. |
| `rdm_engine.cpp` | `src/core/rdm_engine.cpp` | Module state, `rdmSavePoll`, `rdmInitCtrlUid`, `rdmRmtSelect`, `rdmRmtInit`, `putUid`, `rdmBuild`, `rdmTx`, `rdmReadFrame`, `rdmReadResp`. |
| `rdm_types.h` | `include/rdm_types.h` | E1.20 type declarations: UID, command-class enum, PID constants, response-type enum, `rdm_ack_t`, parameter-payload structs, sensor enums. |

## 4. Data Structures

### `RdmState` — global engine state (`rdm_engine.h:27-40`)

| Field | Type | Line | Description |
|---|---|---|---|
| `ctrl` | `rdm_uid_t` | `rdm_engine.h:28` | Controller UID; `man_id` hard-wired to `0x4C58` ('LX'). |
| `lines` | `RdmLine[RDM_MAX_LINES]` | `rdm_engine.h:29` | Registered output lines. |
| `lineN` | `int` | `rdm_engine.h:30` | Count of registered lines (≤ `RDM_MAX_LINES`). |
| `rmt` | `RmtDmx*` | `rdm_engine.h:31` | Active RMT TX handle (set by `rdmRmtSelect`). |
| `de` | `int` | `rdm_engine.h:32` | Active DE/RE GPIO pin. |
| `rx` | `int` | `rdm_engine.h:33` | Active RX pin. |
| `uart` | `uart_port_t` | `rdm_engine.h:34` | Active RX UART (starts as `UART_NUM_2`). |
| `tn` | `uint8_t` | `rdm_engine.h:35` | Transaction counter, incremented per request at `rdm_engine.cpp:81`. |
| `sent` | `volatile uint32_t` | `rdm_engine.h:36` | Total requests sent; incremented at `rdm_engine.cpp:99`. |
| `recv` | `volatile uint32_t` | `rdm_engine.h:37` | Total responses received; incremented at `rdm_engine.cpp:172` and `rdm_task.cpp:113`. |
| `sentMs` | `volatile uint32_t` | `rdm_engine.h:38` | Timestamp of last TX. |
| `recvMs` | `volatile uint32_t` | `rdm_engine.h:39` | Timestamp of last RX. |

### `RdmLine` (`rdm_engine.h:25`)

| Field | Type | Line | Description |
|---|---|---|---|
| `rmt` | `RmtDmx*` | `rdm_engine.h:25` | RMT TX handle for this line. |
| `de` | `int` | `rdm_engine.h:25` | DE/RE GPIO pin number. |
| `rx` | `int` | `rdm_engine.h:25` | UART RX pin number. |
| `uart` | `uart_port_t` | `rdm_engine.h:25` | UART port for RX. |
| `up` | `bool` | `rdm_engine.h:25` | Whether the line has been initialized. |

### `rdm_uid_t` — 48-bit device identity (`rdm_types.h:43-46`)

| Field | Type | Bytes | Line | Description |
|---|---|---|---|---|
| `man_id` | `uint16_t` | 2 | `rdm_types.h:44` | ESTA manufacturer ID (0x0001–0x7fff). |
| `dev_id` | `uint32_t` | 4 | `rdm_types.h:45` | 32-bit device ID within manufacturer. |

### `rdm_ack_t` — parsed response metadata (`rdm_types.h:136-160`)

| Field | Type | Line | Description |
|---|---|---|---|
| `err` | `dmx_err_t` | `rdm_types.h:138` | Non-zero on failure; always `DMX_OK` (0) in practice at `rdm_types.h:132-133`. |
| `size` | `size_t` | `rdm_types.h:139` | Total response packet size in bytes (`msgLen + 2`, set at `rdm_engine.cpp:163`). |
| `src_uid` | `rdm_uid_t` | `rdm_types.h:141` | UID of the responding device. |
| `pid` | `rdm_pid_t` | `rdm_types.h:143` | PID echoed from the request (`m[21..22]`, `rdm_engine.cpp:161`). |
| `type` | `rdm_response_type_t` | `rdm_types.h:147` | ACK / NACK / ACK_TIMER / INVALID / NONE. |
| `message_count` | `int` | `rdm_types.h:149` | Responder's queued-message count (`m[17]`, `rdm_engine.cpp:160`). |
| `pdl` (union) | `size_t` | `rdm_types.h:153` | Parameter data length when type is ACK (`m[23]`, `rdm_engine.cpp:164`). |

### Constants (`rdm_engine.h:16-21`)

| Macro | Value | Line | Purpose |
|---|---|---|---|
| `RDM_SC` | `0xCC` | `rdm_engine.h:16` | RDM start code. |
| `RDM_SC_SUB` | `0x01` | `rdm_engine.h:17` | Sub-start code. |
| `RDM_HDR_LEN` | `24` | `rdm_engine.h:18` | Header length used in the length byte. |
| `RDM_RESP_TIMEOUT_MS` | `9` | `rdm_engine.h:19` | Response receive timeout. |
| `RDM_DISC_TIMEOUT_MS` | `6` | `rdm_engine.h:20` | Discovery read timeout. |
| `IDENTIFY_MS` | `1500` | `rdm_engine.h:21` | Identify LED duration. |
| `RDM_MAX_LINES` | `2` | `rdm_engine.h:13` | Maximum RDM output lines. |

### Module-level globals (`rdm_engine.cpp:14-17`)

| Symbol | Type | Line | Description |
|---|---|---|---|
| `g_rdm` | `RdmState` | `rdm_engine.cpp:14` | Single global engine state. |
| `rdmPollDirty` | `bool` | `rdm_engine.cpp:15` | Dirty flag for NVS poll persistence, checked at `main.cpp:141`. |
| `identifyCh` | `uint16_t` | `rdm_engine.cpp:16` | Channel forced on for fixture identification. |
| `identifyUntil` | `uint32_t` | `rdm_engine.cpp:17` | Millis deadline for identify mode. |

## 5. Concurrency

The transport primitives (`rdmTx`, `rdmReadFrame`, `rdmReadResp`) execute **on core 1** inside the RDM task loop ([core-rdm-task](./core-rdm-task.md), `rdm_task.cpp:14`, priority 18). This ensures the RMT peripheral — shared with the DMX TX path — is never used from two contexts simultaneously (`rdm_engine.h:2-5`).

The UART RX is a dedicated peripheral per RDM line: `RDM_LINE_UART` maps line 0 → UART2 and line 1 → UART1 (`rdm_engine.h:23`). Because the RDM task is the sole owner of the active RMT channel and the active UART, no mutex protects the transport; the single-writer/single-reader UART FIFO (`uart_rx.h`) is lock-free (`uart_rx.h:33-34`).

The module-level globals `g_rdm`, `rdmPollDirty`, `identifyCh`, and `identifyUntil` are read by the WebSocket handler on core 0 (`ws_handler.cpp:14-15` declares them `extern`). `identifyCh`/`identifyUntil` are written from core 0 (`ws_handler.cpp:193-194`) and read from core 1 during TX. This is a benign race: the worst case is one extra or one fewer identify frame, which is acceptable for a visual aid (`rdm_engine.h:45-46`).

## 6. State Machine

No explicit state machine — the engine is a stateless transport. Each `rdmTransaction()` call follows an implicit micro-sequence:

1. **IDLE** — DE pin low (listen), UART flushed (`rdm_engine.cpp:101,109`).
2. **DRIVE** — DE pin set high (`rdm_engine.cpp:102`), RMT transmits the request (`rdm_engine.cpp:103-106`).
3. **WAIT** — `rmt_tx_wait_all_done` blocks up to 60 ms (`rdm_engine.cpp:106`); 90 µs post-TX delay (`rdm_engine.cpp:108`).
4. **LISTEN** — DE pin set low (`rdm_engine.cpp:107`); UART RX collects the response (`rdm_engine.cpp:112-129`).
5. **PARSE** — start-code scan, length/checksum validation, field extraction (`rdm_engine.cpp:131-175`).
6. Return to **IDLE**.

## 7. Entry Points

| Function | Declared | Implemented | Called from |
|---|---|---|---|
| `rdmRmtInit` | `rdm_engine.h:60` | `rdm_engine.cpp:45` | `output_init.cpp` during output bring-up. |
| `rdmRmtSelect` | `rdm_engine.h:57` | `rdm_engine.cpp:36` | `rdm_task.cpp:78` (async relay) and `rdm_task.cpp:67-71` (line select cmd). |
| `rdmBuild` | `rdm_engine.h:64` | `rdm_engine.cpp:73` | `rdm_disc.cpp:23,56,66` and `rdm_task.cpp:26,56`. |
| `rdmTx` | `rdm_engine.h:66` | `rdm_engine.cpp:96` | `rdm_disc.cpp:25,57,68` and `rdm_task.cpp:27,93`. |
| `rdmReadFrame` | `rdm_engine.h:67` | `rdm_engine.cpp:112` | `rdm_engine.cpp:134` and `rdm_task.cpp:100`. |
| `rdmReadResp` | `rdm_engine.h:68` | `rdm_engine.cpp:131` | `rdm_disc.cpp:58` and `rdm_task.cpp:28`. |
| `rdmSavePoll` | `rdm_engine.h:115` | `rdm_engine.cpp:19` | `main.cpp:141` (dirty-flag flush in `loop()`). |
| `rdmInitCtrlUid` | (not in header) | `rdm_engine.cpp:28` | `rdm_engine.cpp:51` (first line init). |

## 8. Data Flow

1. **Controller UID init** — on the first registered output (`rdm_engine.cpp:50-52`), `rdmInitCtrlUid()` reads the ESP32 WiFi MAC via `esp_read_mac()` (`rdm_engine.cpp:30`) and derives `g_rdm.ctrl.dev_id` from bytes 2–5 (`rdm_engine.cpp:31`).
2. **Line registration** — `rdmRmtInit()` (`rdm_engine.cpp:45`) stores the RMT handle, DE pin, RX pin, and UART into `g_rdm.lines[idx]` (`rdm_engine.cpp:56-60`), increments `lineN` (`rdm_engine.cpp:61`), and selects line 0 on first call (`rdm_engine.cpp:62`).
3. **Request build** — `rdmBuild()` (`rdm_engine.cpp:73`) assembles the RDM packet: start code → destination UID → source UID → transaction → message count → sub-device/reserved → command class → PID → PDL → data → checksum.
4. **Transmit** — `rdmTx()` (`rdm_engine.cpp:96`) sets DE high (`rdm_engine.cpp:102`), encodes the frame into RMT symbols (`rdm_engine.cpp:103`), fires `rmt_transmit` (`rdm_engine.cpp:105`), blocks to wait for completion (`rdm_engine.cpp:106`), sets DE low (`rdm_engine.cpp:107`), delays 90 µs (`rdm_engine.cpp:108`), and flushes the RX UART (`rdm_engine.cpp:109`).
5. **Response receive** — `rdmReadFrame()` (`rdm_engine.cpp:112`) polls the UART for up to `RDM_RESP_TIMEOUT_MS` (9 ms, `rdm_engine.h:19`); it scans the byte stream for the `RDM_SC` + `RDM_SC_SUB` pattern (`rdm_engine.cpp:120-121`) and uses the in-packet length byte to determine when the frame is complete (`rdm_engine.cpp:122-124`).
6. **Response parse** — `rdmReadResp()` (`rdm_engine.cpp:131`) validates minimum length (`n < 26` → fail, `rdm_engine.cpp:138`), locates the start-code pair (`rdm_engine.cpp:140-141`), checks the length and 8-bit checksum (`rdm_engine.cpp:147-153`), and extracts the source UID, response type, message count, PID, and PDL (`rdm_engine.cpp:156-165`).
7. **NVS persistence** — `rdmSavePoll()` (`rdm_engine.cpp:19`) writes `stats().rdmCount`, `g_rdm.sent`, and `g_rdm.recv` to the "dmxgw" NVS namespace (`rdm_engine.cpp:22-24`) when flushed by the dirty flag in `main.cpp:141`.

## 9. Protocol Layout

E1.20 RDM packet as assembled by `rdmBuild()` (`rdm_engine.cpp:73-92`):

| Offset | Size | Field | Source |
|---|---|---|---|
| 0 | 1 | Start Code (`0xCC`) | `rdm_engine.cpp:76` |
| 1 | 1 | Sub-start Code (`0x01`) | `rdm_engine.cpp:77` |
| 2 | 1 | Length (`24 + PDL`) | `rdm_engine.cpp:78` |
| 3–8 | 6 | Destination UID (BE) | `rdm_engine.cpp:79` |
| 9–14 | 6 | Source/Controller UID (BE) | `rdm_engine.cpp:80` |
| 15 | 1 | Transaction Number | `rdm_engine.cpp:81` |
| 16 | 1 | Message Count (always `0x01`) | `rdm_engine.cpp:82` |
| 17–20 | 4 | Sub-device + reserved (all `0x00`) | `rdm_engine.cpp:83-84` |
| 21 | 1 | Command Class | `rdm_engine.cpp:85` |
| 22–23 | 2 | Parameter ID (BE) | `rdm_engine.cpp:86-87` |
| 24 | 1 | Parameter Data Length | `rdm_engine.cpp:88` |
| 25…24+PDL | PDL | Parameter Data | `rdm_engine.cpp:88` |
| 25+PDL | 2 | Checksum (BE, additive 8-bit) | `rdm_engine.cpp:89-90` |

**Response parse layout** (as interpreted by `rdmReadResp()`, `rdm_engine.cpp:131-175`) — note that field offsets differ from the build layout by one byte:

| Parsed field | Read from offset | Build offset | Line |
|---|---|---|---|
| msgLen | `m[2]` | 2 | `rdm_engine.cpp:147` |
| source UID | `m[9..14]` | 9–14 | `rdm_engine.cpp:157-158` |
| response type | `m[16]` | 16 = message count | `rdm_engine.cpp:159` |
| message count | `m[17]` | 17 = sub-device | `rdm_engine.cpp:160` |
| PID | `m[21..22]` | 22–23 | `rdm_engine.cpp:161` |
| PDL | `m[23]` | 24 | `rdm_engine.cpp:164` |
| param data | `m[24..]` | 25+ | `rdm_engine.cpp:170` |

Discovery response format (parsed in `rdm_disc.cpp:32-46`): the responder sends a sequence of `0xFE` fill bytes, a `0xAA` separator, 6 AND-ed UID bytes, then a 2-byte complemented checksum.

## 10. Config Integration

| Config field | Section | Live/Reboot | How used |
|---|---|---|---|
| `cfg.rdmMaxDev` | `Config` (`config_schema.h:88`) | Reboot | Caps the discovery result count at `rdm_task.cpp:323`; not read directly by this module. |

The engine itself does not read `Config` fields directly, but `rdm_engine.h:10` includes `config_schema.h` for the `MAX_OUTPUTS` constant used by callers.

## 11. Lifecycle

| Phase | Function | Line | What happens |
|---|---|---|---|
| **Init (per-line)** | `rdmRmtInit` | `rdm_engine.cpp:45` | Registers an RMT channel + DE/RE GPIO + UART RX; on the first line (`idx == 0`), initializes the controller UID via `rdmInitCtrlUid()` (`rdm_engine.cpp:50-52`) and selects line 0 (`rdm_engine.cpp:62`). |
| **Select (per-transaction)** | `rdmRmtSelect` | `rdm_engine.cpp:36` | Copies the chosen line's `rmt`/`de`/`rx`/`uart` into `g_rdm` active pointers (`rdm_engine.cpp:39-42`). Guarded by `line < 0 \|\| line >= g_rdm.lineN` (`rdm_engine.cpp:37`). |
| **Poll (idle)** | — | `rdm_engine.cpp:15` | `rdmPollDirty` flag checked in `main.cpp:141`; when set, `rdmSavePoll()` persists counters. |
| **Shutdown** | — | — | No explicit deinit; the RMT channel and UART are never released (`rdm_engine.h:2-5`), so DMX output continues uninterrupted between RDM ops. |

## 12. Error Handling

| Function | Return type | Failure condition | Line |
|---|---|---|---|
| `rdmRmtInit` | `int` | `g_rdm.lineN >= RDM_MAX_LINES` | `rdm_engine.cpp:46` (returns -1) |
| `rdmRmtSelect` | `void` | `line < 0 \|\| line >= g_rdm.lineN` | `rdm_engine.cpp:37` (early return) |
| `rdmTx` | `void` | `!rd->chan` | `rdm_engine.cpp:98` (early return) |
| `rdmReadFrame` | `int` | Timeout expires with no matching SC | `rdm_engine.cpp:115` (returns 0 or partial) |
| `rdmReadResp` | `bool` | `n < 26` ("short"), no SC ("noSC"), length mismatch ("len"), checksum mismatch ("ck") | `rdm_engine.cpp:138-155` |

No `ESP_LOGE` calls; failures are propagated via `bool`/`int` returns and the `fail` string pointer (`rdm_engine.cpp:136`), which is checked but not logged in the inspected code.

## 13. Allocation

All state is statically allocated in DRAM:

- `g_rdm` — single `RdmState` instance at `rdm_engine.cpp:14`; `RdmState` is 64-bit aligned via `RdmLine`/`RmtDmx` pointers (`rdm_engine.h:27-40`).
- `rdmReadFrame` / `rdmReadResp` use a stack-allocated `rx[96]` buffer (`rdm_engine.cpp:133`).
- `rdmBuild` writes into a caller-provided `buf[64]` (`rdm_task.cpp:24`).
- The RMT symbol buffer is DRAM + DMA-capable, allocated in `dmx_rmt.h:112-113` with `MALLOC_CAP_8BIT | MALLOC_CAP_DMA`.
- No PSRAM or heap allocation occurs in this module.

## 14. Timing

| Constraint | Value | Source |
|---|---|---|
| Response receive timeout | 9 ms | `rdm_engine.h:19` (`RDM_RESP_TIMEOUT_MS`) |
| Discovery read timeout | 6 ms | `rdm_engine.h:20` (`RDM_DISC_TIMEOUT_MS`) |
| Identify duration | 1500 ms | `rdm_engine.h:21` (`IDENTIFY_MS`) |
| Post-TX DE low delay | 90 µs | `rdm_engine.cpp:108` |
| RMT transmit wait (blocking) | 60 ms | `rdm_engine.cpp:106` |
| UART RX read timeout per poll | 1 ms | `uart_rx.h:34` |

The 90 µs post-TX delay (`rdm_engine.cpp:108`) ensures the responder has time to begin transmitting before the RX UART is flushed and armed.

## 15. Traceability

| Claim | Evidence |
|---|---|
| Engine uses RMT-TX for requests and a separate RX-only UART for responses. | `rdm_engine.h:2-5` |
| Controller UID manufacturer ID is `0x4C58`. | `rdm_engine.h:28` |
| Up to 2 RDM lines are supported. | `rdm_engine.h:13` |
| Line 0 uses UART2, line 1 uses UART1. | `rdm_engine.h:23` |
| `rdmInitCtrlUid` reads the WiFi STA MAC. | `rdm_engine.cpp:30` |
| Device ID is derived from MAC bytes 2–5, big-endian. | `rdm_engine.cpp:31` |
| `rdmRmtInit` returns -1 when lines are exhausted. | `rdm_engine.cpp:46` |
| `rdmRmtSelect` guards against out-of-range line index. | `rdm_engine.cpp:37` |
| `rdmBuild` sets the length byte to `24 + pdl`. | `rdm_engine.cpp:78` |
| Transaction counter is an 8-bit incrementing value. | `rdm_engine.cpp:81` |
| Checksum is a simple 8-bit additive sum. | `rdm_engine.cpp:89` |
| DE pin is set high (drive) before RMT TX. | `rdm_engine.cpp:102` |
| RMT transmit blocks up to 60 ms for completion. | `rdm_engine.cpp:106` |
| DE pin is set low (listen) after TX + 90 µs delay. | `rdm_engine.cpp:107-108` |
| UART RX is flushed before and after TX. | `rdm_engine.cpp:101,109` |
| Response parser rejects packets shorter than 26 bytes. | `rdm_engine.cpp:138` |
| Checksum is verified over `msgLen` bytes. | `rdm_engine.cpp:150-152` |
| Source UID is extracted from bytes 9–14 of the response. | `rdm_engine.cpp:157-158` |
| `rdmSavePoll` persists counters to NVS namespace "dmxgw". | `rdm_engine.cpp:21-24` |
| `rdmPollDirty` is flushed from `main.cpp:141`. | `main.cpp:141` |
| `rdmTransaction` and `rdmRmtRawRelay` moved to `rdm_task.cpp`. | `rdm_engine.cpp:177-178` |
| Typed wrappers declared here are implemented in `rdm_typed.cpp`. | `rdm_engine.h:74` |
| Discovery primitives are declared in `rdm_disc.cpp`/`rdm_disc.h`. | `rdm_engine.h:75` |

## 16. Cross-References

- **[core-rdm-task](./core-rdm-task.md)** — dispatches `rdmTransaction()` and `rdmRmtRawRelay()` via the core-1 RDM task; calls `rdmBuild`, `rdmTx`, `rdmReadResp`, and `rdmReadFrame`.
- **[core-rdm-discovery](./core-rdm-discovery.md)** — `rdm_disc.cpp` calls `rdmBuild`, `rdmTx`, `rdmReadResp`, and `uartRxRead` for `DISC_UNIQUE_BRANCH`, `DISC_MUTE`, and `DISC_UN_MUTE`.
- **[core-rdm-typed](./core-rdm-typed.md)** — typed PID wrappers call `rdmTransaction` (the blocking wrapper in `rdm_task.cpp`).
- **[net-art-rdm-resp-queue](./net-art-rdm-resp-queue.md)** — receives completed Art-Net RDM replies pushed by the task at `rdm_task.cpp:121`.
- **[sys-tasks](./sys-tasks.md)** — `dmxTxTask` runs on core 1 at priority 19; the RDM task runs at priority 18 on the same core.
- **[net-artnet-bridge](./net-artnet-bridge.md)** — `handleArtRdm()` calls `rdmArtRawRelayEnqueue` (core 0 → core 1 enqueue).
- **[drv-dmx-rmt-tx](./drv-dmx-rmt-tx.md)** — provides `RmtDmx` struct and `rmtDmxEncode`/`rmt_transmit` called by `rdmTx`.
- **[drv-dmx-uart-rx](./drv-dmx-uart-rx.md)** — provides `uartRxInit`/`uartRxRead`/`uartRxFlush` called by the engine.
- **[drv-gpio-dir](./drv-gpio-dir.md)** — provides `gpioDeInit`/`gpioDeSet` for DE/RE control.
- **[include-headers](./include-headers.md)** — documents `rdm_types.h` types.

## 17. Limitations

- **Build/parse offset mismatch** — `rdmBuild()` places command class at byte 21, PID at 22–23, PDL at 24, and parameter data at 25+ (`rdm_engine.cpp:85-88`), but `rdmReadResp()` reads PID from `m[21..22]`, PDL from `m[23]`, and data from `m[24+]` (`rdm_engine.cpp:161,164,170`). The response-type field is read from `m[16]` which the builder writes as message count (`rdm_engine.cpp:82,159`). This one-byte offset difference between the request builder and response parser may affect interoperability with strict E1.20 responders.
- **Single responder UID** — the engine stores only one controller UID in `g_rdm.ctrl`; multi-bridge configurations are not supported (`rdm_engine.h:28`).
- **No NACK reason parsing** — `rdmReadResp` checks `ack->err` but the `dmx_err_t` enum has only `DMX_OK = 0` (`rdm_types.h:131-133`); NACK reason codes are not decoded into the ack struct.
- **DE/RE GPIO never released** — the RMT channel and UART are claimed for the lifetime of the line, which prevents reusing the same UART for general DMX RX (`rdm_engine.h:4-5`).

## 18. Open Questions

- Not determinable from the inspected source code — whether the one-byte parse offset has caused real-world interoperability issues with specific responder models.
- Not determinable from the inspected source code — the full set of callers that set `cfg.rdmMaxDev` and whether this limit is user-configurable via the web UI.
- Not determinable from the inspected source code — the origin of `rdmWsProcessQueued()` (declared in `ws_handler.cpp:93`) which processes queued WebSocket RDM commands on core 0 (`main.cpp:150`).

## 19. Testing

- **Native host test** — `test/native/rdm_types_test.cpp` tests UID equality, broadcast/max constants, response-type enum values, PID constants, command-class constants, sensor type/unit enums, `DMX_PACKET_SIZE`, and `RDM_ASCII_SIZE_MAX`.
- **Unity unit test** — `test/unit-test/test_rdm_types/test_unit_rdm_types.cpp` covers the same constants and enums using the Unity framework.
- No host tests exist for `rdm_engine.cpp` transport functions (`rdmBuild`, `rdmTx`, `rdmReadResp`), `rdm_disc.cpp` discovery primitives, or `rdm_task.cpp` task dispatch.

## 20. History

- RMT-TX + RX-only UART architecture adopted to fix issue #64 (core-0 network DMA contention corrupting DMX breaks) — `rdm_engine.h:2-5`, `src/drv/dmx_rmt.h:1-9`.
- `rdmTransaction()` and `rdmRmtRawRelay()` extracted from `rdm_engine.cpp` into `rdm_task.cpp` to run on a dedicated core-1 task (priority 18), preventing blocking of the DMX TX task — `rdm_engine.cpp:177-178`.
- `rdmRmtDiscover()` moved from `rdm_disc.cpp` to `rdm_task.cpp` for the same reason — `rdm_disc.cpp:101-104`.
- Types in `rdm_types.h` forked from the esp_dmx library, declared out of the E1.20 standard directly — `rdm_types.h:4-11`.
