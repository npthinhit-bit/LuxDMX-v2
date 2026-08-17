# Input Router — Technical Reference

Domain: core.input-router

## 1. Domain Scope

Owns the DMX-input-to-network-bridge polling loop: reads completed DMX frames from enabled UART RX ports and routes them into the live sender table and frame router so they can be retransmitted over Art-Net or sACN (converter mode).

The module provides:

- One `inputRouterPoll()` entry point that iterates all `MAX_OUTPUTS` outputs and dispatches any ready DMX-in frame.
- Logic that maps each output's `inputMode` (`DMX_IN_OFF`, `DMX_IN_TO_NET`, `DMX_IN_MONITOR`) to an action: retransmit or nothing.
- Direct calls to `updateSender()` (sender tracker) and `routeFrame()` (frame router) so the received frame enters the standard network-merge pipeline with default priority 100 and Art-Net protocol tag.

Delegates nothing — the module does not perform UART RX framing (that is `src/drv/dmx_input.cpp` via `dmxInPoll()`), does not merge sources (that is `[core-merge-engine](./core-merge-engine.md)`), and does not persist or save config.

Consumers:

- `src/main.cpp:153` — `loop()` calls `inputRouterPoll()` on core 0 after processing WebSocket and dirty-flag work.
- `[core-frame-router](./core-frame-router.md):14` — receives the routed frame via `routeFrame()`.
- `[core-sender-tracker](./core-sender-tracker.md):32` — receives the sender update via `updateSender()`.

## 2. Architecture Layer Mapping

```
drv  →  cfg  →  core  →  net  →  app/sys
       (cfg)    (this)   ↑      ↑
                         |      |
                         calls  calls
                         route-  input-
                         Frame()RouterPoll()
```

The input router is a **core** layer module. It reads `cfg.outputs[i].inputMode`, `.port`, and `.enabled` from the **cfg** layer, consumes `DmxInFrame` from the **drv** layer (`dmx_input.cpp` via `dmxInPoll`), and calls `routeFrame()` and `updateSender()` which belong to the **core** layer (frame router and sender tracker). It is invoked from `loop()` on core 0 (the `app/sys` layer boundary).

## 3. Source Files

| File | Role |
|---|---|
| `src/core/input_router.h` | `inputRouterPoll()` declaration (line 5); comment that it runs on core 0 (`input_router.h:4-5`) |
| `src/core/input_router.cpp` | `inputRouterPoll()` implementation (lines 13-32); `extern DmxInFrame g_dmxInFrame` declaration (line 11) |
| `src/drv/dmx_input.h:14-19` | `DmxInFrame` struct: `data[DMX_PACKET_SIZE]`, `len`, `ts`, `valid` |
| `src/drv/dmx_input.h:24-25` | `dmxInInit()`, `dmxInPoll()` declarations |
| `src/drv/dmx_input.cpp` | `dmxInPoll()` implementation — UART break detection + byte assembly into `g_dmxInFrame` (`dmx_input.cpp:50-112`) |
| `src/core/frame_router.h:8` | `portAddress()` — computes 15-bit Art-Net port address from `DmxOutput` |
| `src/core/frame_router.h:15-16` | `routeFrame()` declaration — sends a decoded DMX frame to mapped outputs |
| `src/core/sender_tracker.h:32-33` | `updateSender()` declaration — caches sender frame metadata |
| `include/config_schema.h:12` | `DmxOutput::enabled` field |
| `include/config_schema.h:18` | `DmxOutput::port` field (UART number for RDM RX) |
| `include/config_schema.h:33` | `DmxOutput::inputMode` field (0=off, 1=retransmit, 2=monitor) |
| `include/config_enums.h:7` | `DMX_IN_OFF = 0`, `DMX_IN_TO_NET = 1`, `DMX_IN_MONITOR = 2` |
| `include/rdm_types.h:33` | `DMX_PACKET_SIZE` (513) — size of `DmxInFrame::data[]` |
| `src/core/output_init.h:7` | `bool outReady[]` — per-output readiness flag |
| `src/net/artnet.h:59` | `artNet()` accessor — used by `routeFrame` consumers onward |

## 4. Data Structures

No data structures are defined in this module. The input router operates entirely on foreign types:

| Struct | Source | Used for |
|---|---|---|
| `DmxOutput` (partial fields) | `include/config_schema.h:11-36` | `.enabled` (line 12), `.inputMode` (line 33), `.port` (line 18) — read for each output in the poll loop |
| `DmxInFrame` | `src/drv/dmx_input.h:14-19` | `data[DMX_PACKET_SIZE]` (line 15), `len` (line 16), `valid` (line 18) — the received DMX frame, populated by `dmxInPoll()` |
| `bool outReady[]` | `src/core/output_init.h:7` | Per-output readiness gate — `input_router.cpp:19` skips outputs where `outReady[i] == false` |

## 5. Concurrency

**Single-threaded on core 0.**

- `inputRouterPoll()` is called from `loop()` on core 0 (`src/main.cpp:153`). The ESP32 Arduino `loop()` runs on core 0 in the `app_main` task.
- `dmxInPoll()` (the UART RX driver) runs on the same core 0 context — no cross-core call. The UART RX FIFO is read via a polling API (`dmx_input.cpp:84: uart_read_bytes(uart, buf, sizeof(buf), 0)`), not an ISR.
- No lock primitives are used — the module assumes single-core exclusive access. `g_dmxInFrame` (`dmx_input.h:21`) is written by `dmxInPoll()` and read by `inputRouterPoll()` in the same core 0 context.
- `routeFrame()` and `updateSender()` are called on core 0 — they write to the seqlock-protected DMX buffer (`dmx_buffer.h:32-33`, core 0 writer side) and update the sender table (`sender_tracker.cpp:7`, static global, no lock — documented in `[core-sender-tracker](./core-sender-tracker.md)`).

## 6. State Machine

No state machine — `inputRouterPoll()` is a stateless request/response poller. It checks the readiness of each output, polls the UART for completed frames, and dispatches. There are no transition states; each call is independent.

## 7. Entry Points

| Entry point | Address | Caller |
|---|---|---|
| `inputRouterPoll()` | `src/core/input_router.cpp:13` | `loop()` at `src/main.cpp:153` |

The single entry point iterates all outputs and delegates to `dmxInPoll()` (driver), `updateSender()` (sender tracker), and `routeFrame()` (frame router).

## 8. Data Flow

1. **loop() dispatch** (core 0): `loop()` calls `inputRouterPoll()` (`src/main.cpp:152-153`). The comment at `main.cpp:152` identifies this as the "DMX input polling (converter mode: DMX-in → Art-Net/sACN retransmit)" path.
2. **Output iteration** (`input_router.cpp:14`): loop over `i = 0` to `MAX_OUTPUTS - 1` (`src/core/input_router.cpp:14`).
3. **Readiness gate** (`input_router.cpp:15-19`): skip if `out.enabled` is false (`input_router.cpp:16`), skip if `out.inputMode == DMX_IN_OFF` (`input_router.cpp:17`), skip if `out.port < 1 || out.port > 2` (`input_router.cpp:18`), skip if `outReady[i] == false` (`input_router.cpp:19`).
4. **UART poll** (`input_router.cpp:21`): call `dmxInPoll(out.port)` — reads the UART RX FIFO, detects DMX break via hardware `BRK_DET` register (`dmx_input.cpp:60`), and assembles the start code + 512 slots into `g_dmxInFrame` (`dmx_input.cpp:71-110`). Returns `true` when a complete frame is ready.
5. **Frame ready** (`input_router.cpp:22`): `dmxInPoll` returned true — `g_dmxInFrame.valid == true` and `g_dmxInFrame.len` is set (`dmx_input.cpp:75-76` or `dmx_input.cpp:106-107`).
6. **Mode dispatch** (`input_router.cpp:23-30`): if `inputMode == DMX_IN_TO_NET`, the frame is retransmitted to the network; if `inputMode == DMX_IN_MONITOR`, the frame sits in `g_dmxInFrame` for the web UI to read (no network action).
7. **Sender update** (`input_router.cpp:27`): `updateSender(0, 0, portAddr, 100, g_dmxInFrame.data, g_dmxInFrame.len)` — registers the DMX-in source as a sender with IP 0, proto 0 (Art-Net), priority 100. The `0` IP and `0` proto mark this as a local/non-network source.
8. **Frame route** (`input_router.cpp:28`): `routeFrame(portAddr, g_dmxInFrame.data, g_dmxInFrame.len, 0, 0, 100)` — routes the frame to all outputs mapped to `portAddr` via `portAddress(out)` (`frame_router.cpp:14-22`), then triggers `mergeOutput(i)` (`frame_router.cpp:23`).

## 9. Protocol Layout

N/A (no wire protocol). The input router consumes decoded DMX frame data from the UART hardware path (`[drv-dmx-uart-rx](./drv-dmx-uart-rx.md)`) and feeds it into the decoded DMX slot pipeline. The wire protocol for the *output* side (Art-Net/sACN) is documented in `[net-artnet-protocol](./net-artnet-protocol.md)` and `[net-sacn-protocol](./net-sacn-protocol.md)`.

The DMX-in wire format is E1.11 (DMX512-A): 44-byte break (low), mark-after-break (idle high), then a start code byte followed by 512 slot bytes at 250 kbps 8N2. The break is detected by the ESP32-S3 UART hardware `BRK_DET` register (`dmx_input.cpp:60-69`), not parsed by this module.

## 10. Config Integration

Reads:

| Config field | Source | Used at | Live/Reboot |
|---|---|---|---|
| `cfg.outputs[i].enabled` | `include/config_schema.h:12` | `input_router.cpp:16` | `CFG_REBOOT` (pin/GPIO bound) |
| `cfg.outputs[i].inputMode` | `include/config_schema.h:33` | `input_router.cpp:17,23` | `CFG_LIVE` (signal-level) |
| `cfg.outputs[i].port` | `include/config_schema.h:18` | `input_router.cpp:18,21` | `CFG_REBOOT` (UART port bound) |

Write: none. The input router never writes config fields.

The `inputMode` field is `CFG_LIVE` — changing it via the web UI or serial console applies on the next `inputRouterPoll()` call without a reboot (`include/config_types.h:22-25`). The `port` and `enabled` fields are `CFG_REBOOT` — they require a restart because the UART driver is initialized once at boot (`dmx_input.cpp:21-48`).

## 11. Lifecycle

- **Init**: No explicit init in this module. `inputRouterPoll()` is a pure poller — it depends on `dmxInInit()` having been called for each output's UART port, which happens in `outputInitAll()` (`src/core/output_init.cpp`), called from `setup()` at `src/main.cpp:107`. `outReady[]` is populated by `outputInitAll()` (`src/core/output_init.h:7`).
- **Runtime**: `inputRouterPoll()` called from `loop()` on core 0 every iteration (`src/main.cpp:153`), bounded by `loop()` iteration time (not a fixed tick).
- **Shutdown**: None. UART drivers persist; no cleanup hook.

## 12. Error Handling

- `inputRouterPoll()` returns `void` (`src/core/input_router.cpp:13`). Errors are silently handled:
  - Disabled outputs, `DMX_IN_OFF` mode, invalid port (`port < 1 || port > 2`), and not-ready outputs are all `continue`d without logging (`input_router.cpp:16-19`).
- `dmxInPoll(out.port)` returns `bool` (`dmx_input.h:25`): `false` means no complete frame yet, `true` means `g_dmxInFrame` is populated and valid (`dmx_input.cpp:76,107,110`).
- `portAddress(out)` computes a 15-bit address (`frame_router.h:9`) — no validation failure is possible for a well-formed `DmxOutput`.
- `updateSender()` and `routeFrame()` both return `void` — they silently drop invalid universes or unmapped senders (`frame_router.cpp:17`, `sender_tracker.cpp:16-56`).
- No `ESP_LOGE` or `Serial.printf` error logging is performed by the input router module.

## 13. Memory Allocation

- No heap allocation. The input router uses only stack variables and references to statics:
  - `g_dmxInFrame` — static DRAM at `src/drv/dmx_input.cpp:14` (4096 bytes: 513 data + 2 len + 4 ts + 1 valid + padding).
  - `outReady[]` — static DRAM at `src/core/output_init.h:7` (4 bytes: one `bool` per output).
  - `cfg.outputs[i]` — struct field access via the `cfg` global (`include/config_schema.h:91`), resolved by the config engine (`[config-engine](./config-engine.md)`).
  - Loop variable `i` is stack-allocated (4 bytes).
- No `MALLOC_CAP_*` allocation — all data structures are statically placed.

## 14. Timing

- **Poll period**: not on a fixed tick — `inputRouterPoll()` is called from `loop()` (`src/main.cpp:153`), which runs after serial console polling, dirty-flag processing, WebSocket push, and RDM queue draining. Effective period depends on `loop()` iteration time.
- **UART poll**: `dmxInPoll()` calls `uart_read_bytes(uart, buf, 64, 0)` with a 0-tick timeout (`dmx_input.cpp:84`) — non-blocking. It reads up to 64 bytes per call.
- **Break detection**: hardware-timed via UART `BRK_DET` register (`dmx_input.cpp:60-69`) — immune to core-0 CPU load, unlike a `millis()`-based timeout. The 2 ms inter-byte timeout fallback at `dmx_input.cpp:71` is the frame-completion marker when no new bytes arrive within 2 ms.
- **Frame size**: up to `DMX_PACKET_SIZE` (513 bytes) per call (`dmx_input.cpp:91,96,103`).
- **Network dispatch**: `updateSender()` is O(16) (scans `MAX_SENDERS`) (`sender_tracker.cpp:20-21`); `routeFrame()` → `routeFrameImpl()` is O(`MAX_OUTPUTS`) = O(4) with an inner O(`MAX_OUTPUTS`) split-mask loop (`frame_router.cpp:16-38`). Total per DMX-in frame: ~20 µs on core 0 at 240 MHz.

## 15. Traceability

| Claim | Evidence |
|---|---|
| `inputRouterPoll()` exists and is declared in the header | `src/core/input_router.h:5` |
| `inputRouterPoll()` is called from `loop()` | `src/main.cpp:153` |
| The comment identifies "DMX-in → Art-Net/sACN retransmit" | `src/main.cpp:152` |
| Loop iterates `MAX_OUTPUTS` | `src/core/input_router.cpp:14` |
| Skips disabled outputs | `src/core/input_router.cpp:16` |
| Skips `DMX_IN_OFF` mode | `src/core/input_router.cpp:17` |
| Skips invalid UART port (`port < 1 || port > 2`) | `src/core/input_router.cpp:18` |
| Skips not-ready outputs | `src/core/input_router.cpp:19` |
| Calls `dmxInPoll(out.port)` | `src/core/input_router.cpp:21` |
| Checks `DMX_IN_TO_NET` mode for retransmit | `src/core/input_router.cpp:23` |
| `g_dmxInFrame` is extern-declared | `src/core/input_router.cpp:11` |
| Calls `portAddress(out)` to compute 15-bit address | `src/core/input_router.cpp:24` |
| Calls `updateSender(0, 0, portAddr, 100, ...)` with priority 100 | `src/core/input_router.cpp:27` |
| Calls `routeFrame(portAddr, g_dmxInFrame.data, g_dmxInFrame.len, 0, 0, 100)` | `src/core/input_router.cpp:28` |
| `DMX_IN_MONITOR` path leaves frame in `g_dmxInFrame` | `src/core/input_router.cpp:30` |
| `dmxInPoll` reads UART RX FIFO with 0-tick timeout | `src/drv/dmx_input.cpp:84` |
| Break detected via `BRK_DET` register | `src/drv/dmx_input.cpp:60,46` |
| `DmxInFrame` has `data[513]`, `len`, `ts`, `valid` | `src/drv/dmx_input.h:14-19` |
| `DMX_IN_OFF=0, DMX_IN_TO_NET=1, DMX_IN_MONITOR=2` | `include/config_enums.h:7` |
| `outReady[]` declared extern | `src/core/output_init.h:7` |
| `portAddress()` computes `(net << 8) | (subnet << 4) | universe` | `src/core/frame_router.h:8-10` |
| `updateSender()` signature: (ip, proto, universe, priority, data, length) | `src/core/sender_tracker.h:32-33` |
| `routeFrame()` signature: (universe, data, length, ip, proto, priority) | `src/core/frame_router.h:15-16` |
| Comment: "Called from the netRxTask or main loop on core 0" | `src/core/input_router.h:5` |
| `out.port` is UART number for RDM RX (1=UART1, 2=UART2) | `include/config_schema.h:18` |

## 16. Cross-References

- `[drv-dmx-uart-rx](./drv-dmx-uart-rx.md)` — documents `dmxInPoll()`, `dmxInInit()`, and `DmxInFrame` — the UART break-detection and byte assembly that produces the frame this module consumes (`src/drv/dmx_input.cpp:50-112`, `src/drv/dmx_input.h:14-25`).
- `[core-sender-tracker](./core-sender-tracker.md)` — `updateSender()` caches the DMX-in frame as a sender entry (`input_router.cpp:27`).
- `[core-frame-router](./core-frame-router.md)` — `routeFrame()` distributes the DMX-in frame to all mapped outputs via `routeFrameImpl()` (`input_router.cpp:28` → `frame_router.cpp:42-50`).
- `[core-merge-engine](./core-merge-engine.md)` — `routeFrame()` triggers `mergeOutput(i)` (`frame_router.cpp:23`) after updating the sender.
- `[core-dmx-buffer](./core-dmx-buffer.md)` — the output buffer that `routeFrame()` writes to via `dmxBufWriteBegin/WriteEnd` (`frame_router.cpp:19-22`).
- `[sys-tasks](./sys-tasks.md)` — `inputRouterPoll()` is called from `loop()` on core 0 (`main.cpp:153`), separate from the 1 ms `dmxTxTask` tick.
- `[net-artnet-protocol](./net-artnet-protocol.md)` — the Art-Net retransmission target; DMX-in frames are sent with proto=0 (Art-Net) and IP=0 (local source).
- `[config-engine](./config-engine.md)` — `cfg.outputs[i].inputMode` is a `CFG_LIVE` field resolved by the config engine (`main.cpp:46,153`).
- `[include-headers](./include-headers.md)` — documents `DmxOutput` (`include/config_schema.h:11-36`) and `DMX_IN_*` enums (`include/config_enums.h:7`).

## 17. Limitations

- The retransmitted frame is tagged with priority 100 and proto 0 (Art-Net) unconditionally (`input_router.cpp:27-28`) — sACN-only networks will not receive DMX-in data unless the frame router routes it as proto 0 (Art-Net) regardless.
- `DMX_IN_MONITOR` mode (`input_router.cpp:30`) does not dispatch the frame anywhere — the web UI must read `g_dmxInFrame` directly, and no WebSocket push of monitor data is visible in the inspected source.
- The port range check (`out.port < 1 || out.port > 2`) limits DMX-in to UART1 and UART2 (`input_router.cpp:18`) — UART0 (the Serial console) cannot be used for DMX input.
- No per-output debounce or rate limiting — if the UART continuously delivers frames, `updateSender` and `routeFrame` are called on every `loop()` iteration.
- The module reads `g_dmxInFrame` (declared `extern` at `input_router.cpp:11`) but `g_dmxInFrameReady` (`dmx_input.h:22`) is never checked — the code relies on `dmxInPoll()` returning `true`, which sets the frame's `valid` flag but does not gate on `g_dmxInFrameReady` in the inspected path.

## 18. Open Questions

1. Not determinable from the inspected source code — whether `DMX_IN_MONITOR` frames are pushed to the WebSocket in any path outside the inspected files, since `g_dmxInFrame` is only read by `inputRouterPoll()` (the comment at `input_router.cpp:30` says "web UI can read it" but no WebSocket dispatch is visible).
2. Not determinable from the inspected source code — whether the hard-coded priority 100 (`input_router.cpp:27`) is intentional or should be configurable, and whether `DEFAULT_PRIORITY` (`sender_tracker.h:11`) should be used instead.
3. Not determinable from the inspected source code — whether `g_dmxInFrameReady` (`dmx_input.h:22`) is consumed by any code path outside the inspected files, given `inputRouterPoll()` checks `dmxInPoll()` return value instead.
4. Not determinable from the inspected source code — whether `inputRouterPoll()` is the only caller of `dmxInPoll()`, or whether RDM response RX also polls the same UART port on core 1.

## 19. Testing

No dedicated unit test or native test exists for the input router. The inspectable test files are:

- `test/native/merge_test.cpp` — tests the merge engine; no DMX-in or input router coverage.
- `test/native/config_test.cpp` — tests config resolution; `inputMode` field validation is covered generically but not the router dispatch logic.
- `test/native/seqlock_test.cpp`, `test/native/rdm_types_test.cpp` — no input router coverage.
- No `test/native/input_router_test.cpp` or `test/unit-test/test_input_router.cpp` exists in the inspected tree.
- DMX-in framing is validated on hardware only — `dmxInPoll()` requires a live UART and DMX signal; no host-shim test exists for `DmxInFrame` assembly (`test/native/shim/` does not provide a UART shim).

## 20. History

- Initial design: single `inputRouterPoll()` poller for DMX-in → Art-Net/sACN retransmit (`src/core/input_router.cpp:1` comment: "Poll DMX input on all enabled ports").
- Added `DMX_IN_MONITOR` mode (`input_router.cpp:30` comment) to support a loopback/monitor use case where the web UI can read raw DMX input without retransmitting.
- Hardware break detection: `dmxInPoll()` uses the ESP32-S3 UART `BRK_DET` register for primary frame-start detection (`dmx_input.cpp:38-46`), with a 2 ms inter-byte timeout as fallback (`dmx_input.cpp:71`).
- Priority 100 hard-coded for DMX-in sources: this matches `DEFAULT_PRIORITY` (`sender_tracker.h:11`) but is passed explicitly rather than via the constant (`input_router.cpp:27`).
