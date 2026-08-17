# RDM Task — Technical Reference

Domain: core.rdm_task

## 1. Domain Scope

The RDM Task module owns the **dedicated FreeRTOS task** that executes all timing-critical RDM I/O on core 1, and the **command queue** that bridges core-0 callers (Art-Net bridge, WebSocket handler) to that task. It provides:

- **Task lifecycle** — `rdmTaskInit()` / `rdmTaskDeinit()` create and tear down a core-1 task pinned to priority 18 (`rdm_task.cpp:145-189`).
- **Async enqueue API** — non-blocking wrappers (`rdmTransactionAsync`, `rdmDiscoverAsync`, `rdmMuteAsync`, etc.) push `RdmCmd` structs into an SPSC FreeRTOS queue (`rdm_task.cpp:192-282`).
- **Blocking wrappers** — `rdmTransaction()`, `rdmRmtDiscover()`, `rdmRmtRawRelay()` create a binary semaphore, enqueue the async command, and wait (`rdm_task.cpp:300-351`).
- **Art-Net async relay** — `rdmArtRawRelayEnqueue()` copies an incoming ArtRdm request into the command buffer and enqueues it without blocking core 0 (`rdm_task.cpp:285-297`).
- **Command dispatch** — `rdmTaskLoop()` receives commands and dispatches to the appropriate [core-rdm-engine](./core-rdm-engine.md) / [core-rdm-discovery](./core-rdm-discovery.md) primitive (`rdm_task.cpp:22-138`).

It delegates RMT TX, UART RX, and packet building to [core-rdm-engine](./core-rdm-engine.md) and binary-search discovery to [core-rdm-discovery](./core-rdm-discovery.md).

It is consumed by:
- **net** — `artnet_bridge.cpp:149` calls `rdmArtRawRelayEnqueue()` (core 0 → core 1).
- **ws_handler** — `ws_handler.cpp:104` calls `rdmRmtDiscover()`, `ws_handler.cpp:117` calls `rdmOpSetAddr()` (which calls `rdmTransaction`).
- **main** — `main.cpp:115` calls `rdmTaskInit()`.

## 2. Layer Mapping

| Layer | Module path | Role |
|---|---|---|
| **drv** | `src/drv/dmx_rmt.h`, `src/drv/uart_rx.h`, `src/drv/gpio_dir.h` | Hardware primitives called by the task loop via `rdmTx()`, `rdmReadFrame()`, `uartRxRead()`, `gpioDeSet()`. |
| **cfg** | `include/config_schema.h` | `cfg.rdmMaxDev` read at `rdm_task.cpp:323`. |
| **core** | `src/core/rdm_task.cpp`, `rdm_task.h` | **This module** — task, queue, dispatch, async/blocking API. |
| **net** | `src/net/art_rdm_resp_queue.h` | `artRdmPushResponse()` called at `rdm_task.cpp:121` to relay ArtRdm replies back to core 0. |
| **sys** | `src/sys/tasks.h` | `createTasks()` spawns `dmxTxTask` (core 1, prio 19); the RDM task runs at priority 18 on the same core. |

## 3. Source Files

| File | Path | Role |
|---|---|---|
| `rdm_task.h` | `src/core/rdm_task.h` | Task configuration constants, `RdmCmdType` enum, `RdmCmd`/`RdmTaskState` structs, async and blocking API declarations. |
| `rdm_task.cpp` | `src/core/rdm_task.cpp` | Global `g_rdmTask`, task loop, enqueue APIs, blocking wrappers. |

## 4. Data Structures

### `RdmCmdType` enum (`rdm_task.h:16-23`)

| Enumerator | Value | Handler in task loop |
|---|---|---|
| `RDM_CMD_TRANSACTION` | 0 | `rdm_task.cpp:23` |
| `RDM_CMD_DISCOVER` | 1 | `rdm_task.cpp:37` |
| `RDM_CMD_MUTE` | 2 | `rdm_task.cpp:47` |
| `RDM_CMD_UNMUTE_ALL` | 3 | `rdm_task.cpp:54` |
| `RDM_CMD_SELECT_LINE` | 4 | `rdm_task.cpp:65` |
| `RDM_CMD_RAW_RELAY` | 5 | `rdm_task.cpp:77` |

### `RdmCmd` — command struct (`rdm_task.h:25-61`)

| Field | Type | Line | Used by |
|---|---|---|---|
| `type` | `RdmCmdType` | `rdm_task.h:26` | All handlers |
| `dest` | `rdm_uid_t` | `rdm_task.h:28` | RDM_CMD_TRANSACTION |
| `cc` | `uint8_t` | `rdm_task.h:29` | RDM_CMD_TRANSACTION |
| `pid` | `uint16_t` | `rdm_task.h:30` | RDM_CMD_TRANSACTION |
| `reqPd[32]` | `uint8_t[32]` | `rdm_task.h:31` | RDM_CMD_TRANSACTION (copied at `rdm_task.cpp:210-212`) |
| `reqPdl` | `uint8_t` | `rdm_task.h:32` | RDM_CMD_TRANSACTION |
| `respPd` | `uint8_t*` | `rdm_task.h:33` | RDM_CMD_TRANSACTION (caller-owned) |
| `respMax` | `int` | `rdm_task.h:34` | RDM_CMD_TRANSACTION |
| `respPdl` | `int*` | `rdm_task.h:35` | RDM_CMD_TRANSACTION |
| `ack` | `rdm_ack_t*` | `rdm_task.h:36` | RDM_CMD_TRANSACTION |
| `found` | `rdm_uid_t*` | `rdm_task.h:38` | RDM_CMD_DISCOVER |
| `maxFound` | `int` | `rdm_task.h:39` | RDM_CMD_DISCOVER |
| `foundCount` | `int*` | `rdm_task.h:40` | RDM_CMD_DISCOVER |
| `muteUid` | `rdm_uid_t` | `rdm_task.h:42` | RDM_CMD_MUTE |
| `lineIdx` | `int` | `rdm_task.h:44` | RDM_CMD_SELECT_LINE, RDM_CMD_RAW_RELAY |
| `reqNoSC` | `const uint8_t*` | `rdm_task.h:46` | RDM_CMD_RAW_RELAY (legacy path) |
| `reqLen` | `int` | `rdm_task.h:47` | RDM_CMD_RAW_RELAY (legacy path) |
| `respNoSC` | `uint8_t*` | `rdm_task.h:48` | RDM_CMD_RAW_RELAY (legacy path) |
| `respNoSCMax` | `int` | `rdm_task.h:49` | RDM_CMD_RAW_RELAY (legacy path) |
| `rawRelayResult` | `int*` | `rdm_task.h:50` | RDM_CMD_RAW_RELAY |
| `artReq[261]` | `uint8_t[261]` | `rdm_task.h:55` | RDM_CMD_RAW_RELAY (Art-Net async path — request is **copied** into this buffer at `rdm_task.cpp:292`) |
| `artReqLen` | `uint16_t` | `rdm_task.h:56` | RDM_CMD_RAW_RELAY (Art-Net async path) |
| `artDestIp` | `uint32_t` | `rdm_task.h:57` | RDM_CMD_RAW_RELAY (Art-Net async path) |
| `done` | `SemaphoreHandle_t` | `rdm_task.h:59` | All (completion signalling) |
| `result` | `bool*` | `rdm_task.h:60` | All |

### `RdmTaskState` (`rdm_task.h:63-73`)

| Field | Type | Line | Description |
|---|---|---|---|
| `cmdQueue` | `QueueHandle_t` | `rdm_task.h:64` | SPSC FreeRTOS queue (depth `RDM_QUEUE_LENGTH`). |
| `taskHandle` | `TaskHandle_t` | `rdm_task.h:65` | Task handle for deinit. |
| `running` | `bool` | `rdm_task.h:66` | Lifecycle flag; `false` causes loop exit at `rdm_task.cpp:18`. |
| `discRunning` | `bool` | `rdm_task.h:68` | Reserved for future use; not referenced in inspected handlers. |
| `discOut` | `rdm_uid_t*` | `rdm_task.h:69` | Reserved; not used. |
| `discMax` | `int` | `rdm_task.h:70` | Reserved; not used. |
| `discCount` | `int` | `rdm_task.h:71` | Reserved; not used. |
| `discDone` | `SemaphoreHandle_t` | `rdm_task.h:72` | Reserved; deleted in `rdmTaskDeinit` if non-null (`rdm_task.cpp:184`). |

### Constants (`rdm_task.h:12-14`)

| Macro | Value | Purpose |
|---|---|---|
| `RDM_TASK_STACK_SIZE` | 8192 | Task stack depth. |
| `RDM_TASK_PRIORITY` | 18 | Core-1 priority (below `dmxTxTask` at 19). |
| `RDM_QUEUE_LENGTH` | 32 | Max pending commands. |

### Global (`rdm_task.cpp:12`)

| Symbol | Type | Line | Description |
|---|---|---|---|
| `g_rdmTask` | `RdmTaskState` | `rdm_task.cpp:12` | Single task-state instance. |

## 5. Concurrency

- **Core**: pinned to core 1 via `xTaskCreatePinnedToCore(..., 1)` at `rdm_task.cpp:157`.
- **Priority**: 18 (`RDM_TASK_PRIORITY`, `rdm_task.h:13`), one level below `dmxTxTask` at 19 (`tasks.cpp:83`).
- **Stack**: 8192 bytes (`RDM_TASK_STACK_SIZE`, `rdm_task.h:12`).
- **Queue**: SPSC FreeRTOS queue — single producer is core-0 enqueue code (async API), single consumer is `rdmTaskLoop` on core 1 (`rdm_task.cpp:19`). Queue depth = 32 (`RDM_QUEUE_LENGTH`, `rdm_task.h:14`).
- **Completion**: binary semaphores (`SemaphoreHandle_t done`) — core-0 caller blocks on `xSemaphoreTake` (`rdm_task.cpp:301`), task gives via `xSemaphoreGive` (`rdm_task.cpp:137`).
- **Cross-core reply ring**: for Art-Net RDM, the task pushes completed replies via `artRdmPushResponse()` (`rdm_task.cpp:121`), which writes to the volatile SPSC ring in `art_rdm_resp_queue.cpp:4-6` consumed by `netRxTask` on core 0.
- **Line selection race**: `rdmRmtSelect(cmd.lineIdx)` is deferred to core 1 and guarded by `cmd.artReqLen && cmd.lineIdx >= 0` (`rdm_task.cpp:78`) so two back-to-back ArtNet RDM packets targeting different outputs cannot race.

## 6. State Machine

The task loop has two states:

| State | Condition | Transition |
|---|---|---|
| **RUNNING** | `g_rdmTask.running == true` | Entry at `rdm_task.cpp:154`; loop body at `rdm_task.cpp:18`. Exits to **EXITING** when `deinit` sets `running = false` (`rdm_task.cpp:175`). |
| **WAITING** | Inside `xQueueReceive` with a 10 ms timeout | If a command arrives, dispatch and return to **RUNNING**; if timeout, re-check `running` and re-block. |
| **EXITING** | `running == false` and loop exits | Prints "task exiting" (`rdm_task.cpp:141`) and calls `vTaskDelete(nullptr)` (`rdm_task.cpp:142`). |

The command dispatch within **RUNNING** is a flat `switch` over `cmd.type` (`rdm_task.cpp:22`) with no sub-states.

## 7. Entry Points

| Function | Declared | Called from | Notes |
|---|---|---|---|
| `rdmTaskInit` | `rdm_task.h:77` | `main.cpp:115` (setup phase 6b) | Creates queue + task on core 1. Idempotent: returns `true` if already running (`rdm_task.cpp:146`). |
| `rdmTaskDeinit` | `rdm_task.h:78` | Not called in inspected `main.cpp` | Sets `running = false`, deletes task handle, queue, and `discDone` semaphore. |
| `rdmTransactionAsync` | `rdm_task.h:81` | Called by `rdmTransaction` (`rdm_task.cpp:310`) | Copies request data into `cmd.reqPd` (max 32 bytes, `rdm_task.cpp:210-212`). |
| `rdmDiscoverAsync` | `rdm_task.h:86` | Called by `rdmRmtDiscover` (`rdm_task.cpp:329`) | Passes UID output array pointer + count pointer through the queue. |
| `rdmMuteAsync` | `rdm_task.h:87` | Not called from inspected code | Enqueues a single mute. |
| `rdmUnMuteAllAsync` | `rdm_task.h:88` | Not called from inspected code | Enqueues a broadcast unmute. |
| `rdmSelectLineAsync` | `rdm_task.h:89` | Not called from inspected code | Enqueues a line select. |
| `rdmRawRelayAsync` | `rdm_task.h:90` | Called by `rdmRmtRawRelay` (`rdm_task.cpp:344`) | Legacy blocking relay: caller-owned buffers. |
| `rdmArtRawRelayEnqueue` | `rdm_task.h:98` | `artnet_bridge.cpp:149` (`handleArtRdm`) | Non-blocking Art-Net async path; copies request into `cmd.artReq[261]` (`rdm_task.cpp:292`). |
| `rdmTransaction` (blocking) | `rdm_engine.h:69` | `rdm_typed.cpp` wrappers | `rdm_task.cpp:304`; waits 5000 ms. |
| `rdmRmtDiscover` (blocking) | `rdm_disc.h:23` | `ws_handler.cpp:104` | `rdm_task.cpp:320`; waits 30 000 ms. |

## 8. Data Flow

### Async enqueue (core 0 → core 1)

1. Core-0 caller invokes an async API, e.g. `rdmTransactionAsync()` (`rdm_task.cpp:192`) or `rdmArtRawRelayEnqueue()` (`rdm_task.cpp:285`).
2. The function checks `g_rdmTask.running && g_rdmTask.cmdQueue` (`rdm_task.cpp:196,269,286`); returns `false` immediately if the task is not initialized.
3. A `RdmCmd` is zero-initialized (`rdm_task.cpp:198,287`).
4. Command fields are populated (type, destination UID, PID, request data). For the Art-Net path, the request is **copied** into `cmd.artReq[261]` (`rdm_task.cpp:292`) so the enqueue is truly non-blocking — the caller's packet buffer is never referenced while core 1 runs the multi-ms transaction (`rdm_task.h:51-54`).
5. `xQueueSend()` with a 100 ms timeout (`rdm_task.cpp:214,296`). Returns `false` if the 32-deep queue is full.

### Task dispatch (core 1)

6. `rdmTaskLoop()` (`rdm_task.cpp:14`) blocks on `xQueueReceive()` with a 10 ms timeout (`rdm_task.cpp:19`).
7. The `switch(cmd.type)` dispatches:
   - **RDM_CMD_TRANSACTION** (`rdm_task.cpp:23`): builds a packet via `rdmBuild()` (`rdm_engine.h:64`), transmits via `rdmTx()` (`rdm_engine.h:66`), reads the response via `rdmReadResp()` (`rdm_engine.h:68`), retries up to 3 times with 1 ms gaps (`rdm_task.cpp:25-33`).
   - **RDM_CMD_DISCOVER** (`rdm_task.cpp:37`): calls `rdmUnMuteAll()` (`rdm_disc.h:18`) then `rdmDiscRange()` (`rdm_disc.h:21`), writing the count to `*cmd.foundCount` (`rdm_task.cpp:42`).
   - **RDM_CMD_MUTE** (`rdm_task.cpp:47`): calls `rdmMute()` (`rdm_disc.h:15`) directly.
   - **RDM_CMD_UNMUTE_ALL** (`rdm_task.cpp:54`): builds and transmits a broadcast `DISC_UN_MUTE` (`rdm_task.cpp:56-58`).
   - **RDM_CMD_SELECT_LINE** (`rdm_task.cpp:65`): copies `g_rdm.lines[cmd.lineIdx]` fields into `g_rdm` active pointers (`rdm_task.cpp:67-71`).
   - **RDM_CMD_RAW_RELAY** (`rdm_task.cpp:77`): conditionally calls `rdmRmtSelect(cmd.lineIdx)` (`rdm_task.cpp:78`), then either relays the Art-Net path response via `artRdmPushResponse()` (`rdm_task.cpp:121`) or copies the response into the caller-provided buffer (`rdm_task.cpp:124-125`).

8. Completion is signalled by writing `*cmd.result` (`rdm_task.cpp:136`) and giving `cmd.done` semaphore (`rdm_task.cpp:137`).

### Blocking wrapper (core 0)

9. The blocking `rdmTransaction()` (`rdm_task.cpp:304`) creates a binary semaphore, enqueues via the async API, waits up to 5000 ms (`rdm_task.cpp:315`), deletes the semaphore, and returns the result.
10. `rdmRmtDiscover()` (`rdm_task.cpp:320`) applies the `cfg.rdmMaxDev` ceiling (`rdm_task.cpp:323`) before waiting up to 30 000 ms (`rdm_task.cpp:334`).

## 9. Protocol Layout

N/A — the RDM task does not define its own wire protocol. It carries RDM E1.20 packets whose layout is documented in [core-rdm-engine](./core-rdm-engine.md) §9. The Art-Net relay wraps these in ArtRdm opcode 0x8300 packets at `artnet.cpp:106-116` / `art_rdm_resp_queue.h:13`.

## 10. Config Integration

| Config field | Section | Live/Reboot | How used |
|---|---|---|---|
| `cfg.rdmMaxDev` | `Config` (`config_schema.h:88`) | Reboot | Caps discovery result count in `rdmRmtDiscover()` (`rdm_task.cpp:323`). |

No other `Config` fields are read by this module.

## 11. Lifecycle

| Phase | Function | Line | What happens |
|---|---|---|---|
| **Init** | `rdmTaskInit` | `rdm_task.cpp:145` | Creates a 32-deep queue (`rdm_task.cpp:148`); if creation fails, prints error and returns `false` (`rdm_task.cpp:150-152`). Sets `running = true` (`rdm_task.cpp:154`). Creates the task pinned to core 1 at priority 18 (`rdm_task.cpp:155-158`). |
| **Run** | `rdmTaskLoop` | `rdm_task.cpp:14` | Prints startup banner (`rdm_task.cpp:15`); enters blocking receive loop. |
| **Deinit** | `rdmTaskDeinit` | `rdm_task.cpp:172` | Sets `running = false` (`rdm_task.cpp:175`); deletes task handle (`rdm_task.cpp:177`), queue (`rdm_task.cpp:180`), and `discDone` semaphore if present (`rdm_task.cpp:184`). |

The task is created in `main.cpp:115` (setup phase 6b, after output init and before network protocol init).

## 12. Error Handling

| Function | Return type | Failure condition | Line |
|---|---|---|---|
| `rdmTaskInit` | `bool` | Queue creation fails | `rdm_task.cpp:151` (prints `[RDM] Failed to create command queue`) |
| `rdmTaskInit` | `bool` | Task creation fails | `rdm_task.cpp:161` (prints `[RDM] Failed to create task`) |
| Async APIs | `bool` | `!g_rdmTask.running \|\| !g_rdmTask.cmdQueue` | `rdm_task.cpp:196,217,231,243,254,269,286` |
| Async APIs | `bool` | Queue full (> 32 pending) | `rdm_task.cpp:214,228,240,251,263,296` (100 ms send timeout) |
| `rdmTransaction` | `bool` | Task unavailable or 5 s timeout | `rdm_task.cpp:311,315` |
| `rdmRmtDiscover` | `int` | Task unavailable or 30 s timeout | `rdm_task.cpp:330,336` (returns 0) |
| `rdmRmtRawRelay` | `int` | Task unavailable | `rdm_task.cpp:346` (returns -1) |

All failures are silent except for the init-time serial prints at `rdm_task.cpp:150,161,168`.

## 13. Allocation

| Allocation | Type | Size | Source |
|---|---|---|---|
| `cmd.artReq` | queue-embedded | 261 bytes | `rdm_task.h:55` |
| `cmd.reqPd` | queue-embedded | 32 bytes | `rdm_task.h:31` |
| Queue buffer | FreeRTOS heap | 32 × `sizeof(RdmCmd)` ≈ 3 KB | `rdm_task.cpp:148` |
| Task stack | FreeRTOS heap | 8192 bytes | `rdm_task.cpp:156` |
| `pkt[64]`, `rx[96]` | stack | — | `rdm_task.cpp:24,99` |
| `RDM_TASK_STACK_SIZE` | constexpr | 8192 | `rdm_task.h:12` |

No PSRAM allocation. The `RdmCmd` struct is copied by value into the queue (`xQueueSend` at `rdm_task.cpp:214`), so the caller's local variables need no synchronization.

## 14. Timing

| Constraint | Value | Source |
|---|---|---|
| Task priority | 18 | `rdm_task.h:13` |
| Task stack | 8192 bytes | `rdm_task.h:12` |
| Queue depth | 32 commands | `rdm_task.h:14` |
| Queue send timeout | 100 ms | `rdm_task.cpp:214,296` |
| Transaction retry delay | 1 ms (per attempt) | `rdm_task.cpp:32` (`esp_rom_delay_us(1000)`) |
| Transaction max attempts | 3 | `rdm_task.cpp:25` |
| Blocking transaction timeout | 5000 ms | `rdm_task.cpp:315` |
| Discovery timeout | 30 000 ms | `rdm_task.cpp:334` |
| Queue receive timeout | 10 ms | `rdm_task.cpp:19` |
| RMT TX wait | 60 ms | `rdm_engine.cpp:106` (called at `rdm_task.cpp:27`) |

## 15. Traceability

| Claim | Evidence |
|---|---|
| Task runs on core 1 at priority 18. | `rdm_task.cpp:157` (`xTaskCreatePinnedToCore(..., RDM_TASK_PRIORITY, ..., 1)`) |
| Priority is defined as 18. | `rdm_task.h:13` |
| Stack size is 8192. | `rdm_task.h:12` |
| Queue depth is 32. | `rdm_task.h:14` |
| `RdmCmd` copies the Art-Net request into `artReq[261]`. | `rdm_task.h:55`, `rdm_task.cpp:292` |
| Queue send has a 100 ms timeout. | `rdm_task.cpp:214,296` |
| Line selection is guarded by `cmd.artReqLen && cmd.lineIdx >= 0`. | `rdm_task.cpp:78` |
| Raw relay validates request length against `RDM_HDR_LEN - 1`. | `rdm_task.cpp:85` |
| Art-Net async path caps `pushLen` at 256. | `rdm_task.cpp:120` |
| `rdmRmtDiscover` clamps to `cfg.rdmMaxDev`. | `rdm_task.cpp:323` |
| `rdmSavePoll` is triggered by the dirty flag in `main.cpp`. | `main.cpp:141` |
| `rdmTaskInit` is called in setup phase 6b. | `main.cpp:115` |
| `rdmTransaction` calls in `rdm_typed.cpp` go through the blocking wrapper. | `rdm_typed.cpp:5` |
| `rdmTransaction()` and `rdmRmtRawRelay()` were moved here from `rdm_engine.cpp`. | `rdm_engine.cpp:177-178` |

## 16. Cross-References

- **[core-rdm-engine](./core-rdm-engine.md)** — provides `rdmBuild()`, `rdmTx()`, `rdmReadFrame()`, `rdmReadResp()`, `rdmRmtSelect()`, and `g_rdm` state; declares `rdmTransaction()` / `rdmRmtRawRelay()` signatures.
- **[core-rdm-discovery](./core-rdm-discovery.md)** — `rdm_disc.h` declares `rdmRmtDiscover()` (implemented here at `rdm_task.cpp:320`); `rdmMute()`, `rdmUnMuteAll()`, `rdmDiscRange()` are called directly on the task thread.
- **[core-rdm-typed](./core-rdm-typed.md)** — the typed `rdmOp*()` wrappers call the blocking `rdmTransaction()` at `rdm_task.cpp:304`.
- **[net-art-rdm-resp-queue](./net-art-rdm-resp-queue.md)** — `artRdmPushResponse()` (core 1 producer) is called at `rdm_task.cpp:121`; `artRdmDrainResponses()` (core 0 consumer) runs in `netRxTask` at `tasks.cpp:152`.
- **[net-artnet-bridge](./net-artnet-bridge.md)** — `handleArtRdm()` at `artnet_bridge.cpp:149` calls `rdmArtRawRelayEnqueue()` to enqueue non-blockingly from core 0.
- **[sys-tasks](./sys-tasks.md)** — `createTasks()` spawns `dmxTxTask` (core 1, prio 19, `tasks.cpp:83`); the RDM task runs at prio 18, one level below.
- **[include-headers](./include-headers.md)** — documents `rdm_types.h` types used in `RdmCmd`.

## 17. Limitations

- **Single command queue** — all RDM operations (transactions, discovery, mute, raw relay) share the same 32-deep queue (`rdm_task.h:14`); a long-running discovery sweep blocks transaction commands behind it.
- **Stack buffer for raw relay** — the `RDM_CMD_RAW_RELAY` handler uses a `static uint8_t pkt[264]` (`rdm_task.cpp:86`), making the handler non-reentrant; concurrent raw relay commands would corrupt the buffer.
- **No command cancellation** — once enqueued, a command cannot be withdrawn; if `running` flips to `false` mid-execution the task simply finishes the current command and then exits.
- **`RdmTaskState.discRunning` / `discOut` / `discMax` / `discCount` are reserved** — these fields are declared (`rdm_task.h:68-71`) but never read by any inspected handler.
- **Blocking wrapper allocates a semaphore per call** — `rdmTransaction()` and `rdmRmtDiscover()` each call `xSemaphoreCreateBinary()` (`rdm_task.cpp:307,325`); on a tight loop this could fragment the FreeRTOS heap, though `vSemaphoreDelete` is called on every path (`rdm_task.cpp:316,335`).
- **`rdmMuteAsync`, `rdmUnMuteAllAsync`, `rdmSelectLineAsync` have no inspected callers** — these async APIs are declared (`rdm_task.h:87-89`) but the grep of the source tree found no call sites outside the task loop itself.

## 18. Open Questions

- Not determinable from the inspected source code — whether `rdmTaskDeinit()` is called from any shutdown or reboot path; it is not invoked in `main.cpp`'s `setup()` or `loop()`.
- Not determinable from the inspected source code — how `g_pendingAction` in `ws_handler.cpp` (actions 4–5: set personality, set label) reaches the typed wrappers — the dispatch path is in `ws_handler.cpp:128-135`, which is outside the RDM core.
- Not determinable from the inspected source code — the exact semantics of `RDM_CMD_RAW_RELAY` legacy path (`cmd.reqNoSC`/`cmd.respNoSC`) vs. the Art-Net async path (`cmd.artReq`/`cmd.artReqLen`); both share the same handler at `rdm_task.cpp:77-133`.

## 19. Testing

- No host-native or Unity unit tests exist for `rdm_task.cpp` (the task loop, queue, or async/blocking wrappers). The RDM command queue is only exercised on real hardware via the WebSocket RDM path.
- `rdm_types.h` constants used by `RdmCmd` are covered by `test/native/rdm_types_test.cpp` and `test/unit-test/test_rdm_types/test_unit_rdm_types.cpp` (see [core-rdm-engine](./core-rdm-engine.md) §19).

## 20. History

- `rdmTransaction()` and `rdmRmtRawRelay()` were extracted from `rdm_engine.cpp` into a dedicated core-1 task to prevent ~3 ms blocking per RDM call on the DMX TX task — `rdm_engine.cpp:177-178`, `rdm_task.h:2-3`.
- The Art-Net async path (`rdmArtRawRelayEnqueue`, `RDM_CMD_RAW_RELAY` with `artReq`/`artReqLen`) was added to replace the old synchronous `rdmRmtRawRelay()` call in `handleArtRdm()` that stalled core 0 for up to ~5 s per RDM transaction — `rdm_task.h:94-98`, `artnet_bridge.cpp:140-149`.
- Line selection (`rdmRmtSelect`) was moved from core 0 (`handleArtRdm`) to the core-1 task handler, guarded by `cmd.artReqLen && cmd.lineIdx >= 0`, to prevent a race condition when two back-to-back ArtNet RDM packets target different outputs — `rdm_task.cpp:78`.
