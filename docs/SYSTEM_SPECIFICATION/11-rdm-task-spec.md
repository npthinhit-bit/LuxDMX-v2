# RDM Task - System Specification

Domain: core.rdm_task

## 1. Module Overview

Owns the dedicated FreeRTOS task that executes all timing-critical RDM (E1.20) controller
I/O on core 1, and the single-producer / single-consumer command queue that bridges core-0
callers (the Art-Net bridge and the WebSocket handler) to that task.

The module's responsibilities:

- **Task lifecycle** -- create and tear down a core-1 task pinned to priority 18, one level
  below the DMX TX task (priority 19) that shares the RMT/UART/GPIO hardware cluster.
- **Async enqueue API** -- non-blocking wrappers that push a command struct into the SPSC
  queue so core-0 callers never stall on a multi-millisecond RMT TX/RX transaction.
- **Blocking wrappers** -- create a binary semaphore, enqueue the command, and wait for
  completion, preserving backward compatibility for callers that expect a synchronous result.
- **Art-Net async relay** -- receive an incoming ArtRdm request from core 0, copy it into the
  queue-owned buffer (so the caller's packet is never referenced while core 1 runs the
  transaction), and enqueue it without blocking core 0.
- **Command dispatch** -- a flat switch over the command type, where each handler invokes the
  low-level transport primitives (request build, RMT transmit, response read, discovery
  range, mute) declared by the RDM engine and discovery modules.
- **Response relay** -- push completed Art-Net replies onto the cross-core SPSC response ring
  so the core-0 network task can drain and transmit them.

Line selection is performed on core 1, guarded by a validity check on the relay request
length and line index, so that two back-to-back ArtNet RDM packets targeting different
outputs cannot race. The task never dispatches to itself.

## 2. External Interfaces

### Entry points declared by this module

| Interface | Direction | Purpose |
|---|---|---|
| rdmTaskInit | Called from system bring-up | Create command queue and spawn the core-1 task. Returns bool; idempotent. |
| rdmTaskDeinit | Called from shutdown paths | Signal task exit, delete handle, queue, and any discovery semaphore. |
| rdmTransactionAsync | Called by rdmTransaction and typed wrappers | Non-blocking enqueue of a GET/SET transaction (6-field copy of request data). |
| rdmDiscoverAsync | Called by rdmRmtDiscover | Non-blocking enqueue of a discovery sweep (UID output array passed through the queue). |
| rdmMuteAsync | Internal use | Non-blocking enqueue of a single DISC_MUTE. |
| rdmUnMuteAllAsync | Internal use | Non-blocking enqueue of a broadcast DISC_UN_MUTE. |
| rdmSelectLineAsync | Internal/async use | Non-blocking enqueue of an active-line selection by index. |
| rdmRawRelayAsync | Called by rdmRmtRawRelay (legacy) | Non-blocking enqueue of a raw relay with caller-owned buffers. |
| rdmArtRawRelayEnqueue | Called by the Art-Net bridge (handleArtRdm) | Non-blocking enqueue of an ArtRdm request (copied) plus destination IP and line index. |
| rdmTransaction (blocking) | Called by typed-PID wrappers and the WebSocket handler | Enqueue + semaphore wait (5 s). Returns parsed response. |
| rdmRmtDiscover (blocking) | Called by the WebSocket handler | Enqueue + semaphore wait (30 s budget), clamped to the configured device ceiling. Returns device count. |
| rdmRmtRawRelay (blocking) | Legacy relay callers | Enqueue + semaphore wait (5 s). Returns response length or -1. |

### Command types dispatched in the task loop

| Command | Handler action |
|---|---|
| TRANSACTION | Build packet (rdmBuild), transmit (rdmTx), read response (rdmReadResp), retry up to 3 attempts with 1 ms gaps. |
| DISCOVER | Unmute all, then run discovery range binary search, writing the found count back through the queue. |
| MUTE | Call rdmMute directly (already on the task thread). |
| UNMUTE_ALL | Build and transmit a broadcast DISC_UN_MUTE, 1 ms settle, flush RX UART. |
| SELECT_LINE | Copy the chosen line's RMT handle, DE/RE pin, RX pin, and UART into the active transport state. |
| RAW_RELAY | Guarded line selection, then either relay the reply via the Art-Net response ring (async path) or copy into the caller-provided buffer (legacy path). |

### Completion contract

Every command that carries a completion semaphore is signalled exactly once: the result
bool is written, then the semaphore is given. Callers pass their own semaphore when they
need to block; the pure async paths (mute, unmute all, select line) also accept a semaphore
and result pointer and are signalled identically.

## 3. State Machine

The task loop is a single blocking-wait dispatcher with no persistent substates. Conceptually:

- **IDLE** -- blocked on xQueueReceive with a 10 ms timeout. If deinit clears the running
  flag and no command is pending, the loop exits.
- **DISPATCH** -- a command was received; the flat switch routes it to the appropriate
  low-level primitive. Each handler runs to completion on core 1, then writes the result and
  signals the completion semaphore (if any).
- **EXITING** -- running flag is false and the queue is drained of pending receives; the
  task prints an exit banner and deletes itself.

There are no transitional states: discovery sweeps, mute sequences, and raw relay exchanges
are fully contained within a single DISPATCH cycle. A reserved discRunning flag in the task
state is not used by any handler.

## 4. Data Flow

### Async enqueue (core 0 to core 1)

1. The core-0 caller invokes an async API (e.g. rdmTransactionAsync or
   rdmArtRawRelayEnqueue).
2. The function verifies the task is running and the queue exists; it returns false
   immediately if not.
3. A command struct is zero-initialized and populated with the command parameters.
   For the Art-Net path, the request bytes are copied into the queue-owned art-request
   buffer (261 bytes) so the caller's packet buffer is never referenced during the multi-
   millisecond transaction on core 1.
4. xQueueSend with a 100 ms timeout pushes the struct by value. Returns false if the
   32-deep queue is full.

### Task dispatch (core 1)

5. The task loop blocks on xQueueReceive with a 10 ms timeout, then switches over the
   command type and calls the appropriate low-level primitive (rdmBuild, rdmTx,
   rdmReadResp, discovery range, rdmMute).
6. Completion is signalled by writing the result pointer and giving the completion semaphore.

### Blocking wrapper (core 0)

7. The caller (e.g. rdmTransaction) creates a binary semaphore, enqueues the command via the
   async API, waits up to the wrapper timeout (5 s for transactions, 30 s for discovery),
   deletes the semaphore, and returns. rdmRmtDiscover applies the configured device ceiling
   before waiting.

### Art-Net response relay (core 1 to core 0)

8. On the RAW_RELAY path, the task validates the response length, then pushes the no-
   start-code reply (capped at 256 bytes) via artRdmPushResponse onto the cross-core
   response ring; the core-0 network task drains and transmits it as the ArtRdm reply.

## 5. Configuration Integration

| Config field | Layer | Live / Reboot | How used |
|---|---|---|---|
| rdmMaxDev | Config struct | Reboot | Caps the discovery result count in the blocking rdmRmtDiscover wrapper; if set and smaller than the requested ceiling, it overrides it. |

No other Config fields are consumed by this module. The RDM line table, RMT handle, DE/RE
pin, RX pin, and UART port are owned by the RDM engine and read there.

## 6. Lifecycle

- **Init** -- rdmTaskInit creates the 32-deep command queue, sets the running flag, and spawns
  the pinned task on core 1 at priority 18. Returns false (with a serial diagnostic) if
  queue or task creation fails. Idempotent: a second call returns true immediately.
- **Run** -- the task prints a startup banner and enters the blocking receive loop. Each
  command is dispatched and completed before the next is read.
- **Deinit** -- clears the running flag (causing the loop to exit), deletes the task handle
  (or sets the flag for self-deletion), deletes the queue, and deletes the reserved
  discovery semaphore if present.

The task is spawned from system bring-up phase 6b (after output initialization and before
network protocol init), so all RMT/UART hardware is claimed and the line table is populated.

## 7. Error Handling

| Interface | Returns | Failure condition |
|---|---|---|
| rdmTaskInit | bool | Command-queue creation fails; task creation fails. |
| Async APIs | bool | Task not running or queue is null; queue full (>32 pending within 100 ms send timeout). |
| rdmTransaction | bool | Task unavailable, or 5 s semaphore timeout expires. |
| rdmRmtDiscover | int | Task unavailable, or 30 s semaphore timeout expires (returns 0). |
| rdmRmtRawRelay | int | Task unavailable (returns -1); 5 s semaphore timeout. |
| rdmRmtSelect | -- | Line index out of range -- early return, no effect. |
| RAW_RELAY handler | -- | Active RMT channel null -- drops the command; request length out of range (below header minimum or above 260) -- drops silently. |

Init failures emit a serial diagnostic. All in-flight failures are silent; callers infer
failure from a false result, 0 count, or -1 length. There is no per-command retry at the
queue level. Retry logic lives inside the TRANSACTION handler (3 attempts) and the RAW_RELAY
handler (3 attempts on unicast, 1 on broadcast).

## 8. Timing Constraints

| Constraint | Value | Scope |
|---|---|---|
| Task priority | 18 | Core 1 (below DMX TX task at 19) |
| Task stack | 8192 bytes | FreeRTOS heap |
| Command queue depth | 32 entries | SPSC, sizeof(command struct) each |
| Queue send timeout | 100 ms | Async enqueue |
| Queue receive timeout | 10 ms | Blocking receive in loop |
| Transaction retry delay | 1 ms per attempt | Between build/tx/read attempts |
| Transaction max attempts | 3 | Per TRANSACTION command |
| RMT transmit wait | 60 ms | Per rdmTx call |
| Blocking transaction timeout | 5000 ms | rdmTransaction semaphore wait |
| Discovery timeout | 30000 ms | rdmRmtDiscover semaphore wait |
| Art-Net response cap | 256 bytes | Reply length clamped before relay |
| Response ring | 8 x 260 bytes | Cross-core SPSC replay ring |

All timing is enforced on core 1, isolating the real-time DMX TX path from core-0
networking latency.

## 9. Memory and Allocation Model

- **Command queue buffer** -- FreeRTOS heap; 32 slots of sizeof(command struct). The struct
  is copied by value on enqueue, so callers need no synchronization on their locals.
- **Task stack** -- 8192 bytes, FreeRTOS heap.
- **Queue-embedded buffers** -- the command struct carries a 261-byte art-request copy and a
  32-byte request-data copy, both living inside each queue slot (no extra allocation).
- **Per-handler stacks** -- pkt[64] and rx[96] are stack-allocated inside the TRANSACTION
  and RAW_RELAY handlers.
- **RAW_RELAY static buffer** -- a static uint8_t pkt[264] lives inside the handler, making
  it non-reentrant; concurrent RAW_RELAY commands would share it.
- **Response ring** -- the Art-Net replay ring (8 entries x 260 bytes) is volatile-backed
  with a memory barrier on writes; allocated in the net.art-rdm-resp-queue module.
- No PSRAM allocation. No caller-supplied heap is retained by the task after a command
  completes. Output buffers are caller-owned and written before the semaphore is signalled.

## 10. Safety Considerations

- **Core-1 hardware isolation** -- RMT, UART, and DE/RE GPIO are never accessed from two
  cores simultaneously; all transport primitives execute on the dedicated RDM task,
  eliminating races on the shared signal cluster.
- **Non-blocking enqueue contract** -- the Art-Net request is copied into the queue-owned
  buffer before the caller returns, so a core-0 caller can reuse or discard its packet
  buffer immediately. The core-1 transaction never references caller-owned memory.
- **Line-selection race guard** -- line selection is deferred to core 1 and guarded by a
  validity check on the relay request length and a non-negative line index, preventing two
  back-to-back ArtNet RDM packets targeting different outputs from interleaving.
- **No self-dispatch** -- the task never enqueues a command for itself; external callers use
  async/blocking wrappers, while the task handlers use low-level primitives directly.
- **Semaphore hygiene** -- every blocking wrapper deletes its semaphore on all paths,
  including the enqueue-failure path, preventing FreeRTOS handle leaks.
- **Frame-size bounding** -- raw relay request/response lengths are range-checked against the
  RDM header minimum and the 260-byte maximum before any hardware access.
- **DMX output continuity** -- the DE/RE pin is returned to listen after each transaction,
  and the RMT channel carrying the live DMX stream is never released, so RDM activity
  cannot corrupt the DMX output.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| drv.dmx-rmt-tx | used by | RMT handle, symbol encoding, transmit/fire/wait primitives (via rdmTx) |
| drv.dmx-uart-rx | used by | UART flush and read primitives (via uartRxFlush, rdmReadResp) |
| drv.gpio-dir | used by | DE/RE GPIO direction set (drive/listen) |
| core.rdm-engine | provides primitives; declares wrapper signatures | rdmBuild, rdmTx, rdmReadResp, rdmReadFrame, rdmRmtSelect, the line table; the blocking rdmTransaction / rdmRmtRawRelay signatures live here but are implemented in this module |
| core.rdm-discovery | used by | discovery range binary search, rdmMute, rdmUnMuteAll; rdmRmtDiscover declared here, implemented in this module |
| core.rdm-typed | calls rdmTransaction | Typed PID wrappers (GET/SET per PID) |
| net.artnet-bridge | calls rdmArtRawRelayEnqueue | handleArtRdm enqueues ArtRdm requests non-blockingly from core 0 |
| net.art-rdm-resp-queue | written (core 1), read (core 0) | artRdmPushResponse writes replies; netRxTask drains the replay ring |
| cfg (config_schema) | read | rdmMaxDev device ceiling |
| include.rdm-types | used by | rdm_uid_t, rdm_ack_t, command-class, PID, and start-code constants |
| sys.tasks | schedules | createTasks spawns dmxTxTask at priority 19; this task runs at priority 18 |

## 12. Testing Verification

No host-native, Unity, or native test coverage exists for this module. There is no
rdm_task_test in the native or unit-test trees; the command queue, dispatch loop, async
enqueue, and blocking-wrapper semaphore logic are exercised only on hardware through the
WebSocket RDM path and the Art-Net bridge relay path.

What IS covered by existing tests:
- rdm_types_test -- UID pack/unpack, PID constants, command-class enums, and DMX_PACKET_SIZE.
  These constants are used inside the command struct and dispatch logic, but the struct
  logic itself is not tested off-target.

## 13. Open Questions

1. Whether rdmTaskDeinit is invoked from any shutdown or reboot path; it is declared but the
   inspected bring-up/teardown sequence only calls rdmTaskInit.
2. How the WebSocket handler's pending RDM actions (personality, label writes) reach the
   typed wrappers and ultimately the task dispatch queue. That dispatch glue lives in the
   WebSocket handler, outside this module's boundary.
3. The exact intended distinction between the legacy RAW_RELAY path (caller-owned buffers)
   and the Art-Net async path (queue-copied request). Both share the same RAW_RELAY handler
   but carry data differently.
4. Whether rdmMuteAsync, rdmUnMuteAllAsync, and rdmSelectLineAsync are reachable from any
   production caller, since the inspected call sites only go through the async enqueue from
   within the blocking wrappers and the Art-Net relay.

## 14. History

- **Task extraction**: rdmTransaction and rdmRmtRawRelay were moved out of the RDM engine
  into a dedicated core-1 task (priority 18) so blocking RDM calls can no longer stall the
  DMX TX task. A single ~3 ms transaction on the TX path is no longer possible.
- **Art-Net async relay**: rdmArtRawRelayEnqueue was added to replace the old synchronous
  call in the Art-Net bridge that stalled core 0 for up to ~5 s per RDM transaction. The
  request is now copied into the queue so core 0 returns immediately.
- **Core-1 line selection**: rdmRmtSelect was moved from core 0 (handleArtRdm) to the core-1
  task handler, guarded by a validity check on the relay request length and line index, to
  prevent a race when back-to-back ArtNet RDM packets target different outputs.
- **Single-queue design**: all RDM operations (transactions, discovery, mute, raw relay)
  share one command queue. A long-running discovery sweep can starve transaction commands
  queued behind it.