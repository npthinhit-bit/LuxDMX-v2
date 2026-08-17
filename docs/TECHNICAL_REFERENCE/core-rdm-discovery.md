# RDM Discovery — Technical Reference

Domain: core.rdm_disc

## 1. Domain Scope

The RDM Discovery module implements the E1.20 `DISC_UNIQUE_BRANCH` (0x0001) binary-search discovery algorithm and the associated `DISC_MUTE`/`DISC_UN_MUTE` controls. It owns:

- **UID packing/unpacking** — converting between the 48-bit `rdm_uid_t` struct and a 64-bit sortable integer (`rdm_disc.cpp:8-14`).
- **Branch sweep** — sending a single `DISC_UNIQUE_BRANCH` request and parsing the collision/multi-response into a single UID or an "empty" / "collision" signal (`rdm_disc.cpp:16-50`).
- **Mute / un-mute** — quelling individual responders so they drop out of subsequent sweeps (`rdm_disc.cpp:52-71`).
- **Range search** — the iterative binary search that subdivides the UID space until every responder is found and muted (`rdm_disc.cpp:73-99`).

It delegates transport to [core-rdm-engine](./core-rdm-engine.md) — `rdm_disc.cpp:1-2` includes `rdm_engine.h` and calls `rdmBuild()`, `rdmTx()`, `rdmReadResp()`, `uartRxRead()`, and `gpioDeSet()`.

It is consumed by [core-rdm-task](./core-rdm-task.md), which dispatches a `RDM_CMD_DISCOVER` job to the core-1 RDM task (`rdm_task.cpp:41`). The `rdmRmtDiscover()` entry point is declared in `rdm_disc.h:23` and implemented as a blocking wrapper in `rdm_task.cpp:320`.

## 2. Layer Mapping

| Layer | Module path | Role |
|---|---|---|
| **drv** | `src/drv/dmx_rmt.h`, `src/drv/uart_rx.h`, `src/drv/gpio_dir.h` | RMT TX for the broadcast request, UART RX for the collision response, DE/RE GPIO toggling. |
| **cfg** | `include/config_schema.h` | `MAX_OUTPUTS` constant; not read directly. |
| **core** | `src/core/rdm_disc.cpp`, `rdm_disc.h`, `include/rdm_types.h` | **This module** — discovery algorithm, branch/mute primitives. |
| **net** | `src/net/art_rdm_resp_queue.h` | Not used; discovery results stay on core 1. |
| **sys** | `src/sys/tasks.h` | `rdmTaskLoop` (core 1, prio 18) is the sole caller of `rdmDiscRange` via `RDM_CMD_DISCOVER` (`rdm_task.cpp:41`). |

## 3. Source Files

| File | Path | Role |
|---|---|---|
| `rdm_disc.h` | `src/core/rdm_disc.h` | Public API: `uidPack`, `uidUnpack`, `rdmDiscBranch`, `rdmMute`, `rdmUnMuteAll`, `rdmDiscRange`, `rdmRmtDiscover`. |
| `rdm_disc.cpp` | `src/core/rdm_disc.cpp` | Implementation of all non-trivial discovery primitives; `rdmRmtDiscover` is in `rdm_task.cpp`. |

## 4. Data Structures

This module uses only the opaque `rdm_uid_t` type from [core-rdm-engine](./core-rdm-engine.md) (`rdm_types.h:43-46`) and the engine's `RdmState` globals (`g_rdm.lines`, `g_rdm.uart`, `g_rdm.de`). No module-private structs exist.

The binary search uses two **static** stack arrays at `rdm_disc.cpp:74`:

| Symbol | Type | Line | Capacity | Purpose |
|---|---|---|---|---|
| `stkLo` | `uint64_t[64]` | `rdm_disc.cpp:74` | 64 | Lower-bound UID stack. |
| `stkHi` | `uint64_t[64]` | `rdm_disc.cpp:74` | 64 | Upper-bound UID stack. |

## 5. Concurrency

All discovery primitives execute **on core 1** inside `rdmTaskLoop()` ([core-rdm-task](./core-rdm-task.md), `rdm_task.cpp:14`). The task is the sole owner of the active RMT channel and RX UART (`rdm_engine.h:2-5`), so no mutex is required.

`rdm_disc.cpp` reads `g_rdm.uart` and `g_rdm.de` directly (`rdm_disc.cpp:27,28,70`) without a lock, which is safe because only the RDM task touches these fields.

The `static` qualifier on the stack arrays (`rdm_disc.cpp:74`) means they persist across calls within the same task context; no re-entrancy protection is needed because the discovery path is single-threaded by design.

## 6. State Machine

**No state machine** — the binary search is a single iterative loop in `rdmDiscRange()` (`rdm_disc.cpp:73-99`). It maintains an explicit stack of UID-range pairs and a per-call budget counter. The loop has three conditions for each branch result:

- **Return 0 (empty)** → range contains no responders; pop next range.
- **Return 1 (one UID found)** → attempt to mute the device; if unmuted, record it; continue sweeping the same range.
- **Return 2 (collision)** → split the range at the midpoint and push both halves onto the stack.

There is no persistent discovery state across calls; each invocation of `rdmRmtDiscover()` starts fresh with `rdmUnMuteAll()`.

## 7. Entry Points

| Function | Declared | Implemented | Called from |
|---|---|---|---|
| `rdmDiscRange` | `rdm_disc.h:21` | `rdm_disc.cpp:73` | `rdm_task.cpp:41` (RDM_CMD_DISCOVER). |
| `rdmUnMuteAll` | `rdm_disc.h:18` | `rdm_disc.cpp:64` | `rdm_task.cpp:39` (RDM_CMD_DISCOVER) and `rdm_task.cpp:28` (RDM_CMD_UNMUTE_ALL). |
| `rdmMute` | `rdm_disc.h:15` | `rdm_disc.cpp:52` | `rdm_disc.cpp:85` (inside `rdmDiscRange`) and `rdm_task.cpp:50` (RDM_CMD_MUTE). |
| `rdmDiscBranch` | `rdm_disc.h:12` | `rdm_disc.cpp:16` | `rdm_disc.cpp:82` (inside `rdmDiscRange`). |
| `rdmRmtDiscover` | `rdm_disc.h:23` | `rdm_task.cpp:320` | [core-rdm-task](./core-rdm-task.md) blocking wrapper. |

## 8. Data Flow

1. **Dispatch** — core-0 code (WebSocket handler at `ws_handler.cpp:104` or Art-Net bridge) calls the blocking `rdmRmtDiscover()` wrapper in `rdm_task.cpp:320`, which enqueues a `RDM_CMD_DISCOVER` command to the core-1 RDM task queue (`rdm_task.cpp:41`).
2. **Initial un-mute** — the task handler calls `rdmUnMuteAll()` (`rdm_task.cpp:39`), which builds a broadcast `DISC_UN_MUTE` (`rdm_engine.h:72`/`rdm_engine.cpp:66`) and transmits it over RMT (`rdm_disc.cpp:68`), then delays 1 ms (`rdm_disc.cpp:69`) and flushes the UART (`rdm_disc.cpp:70`).
3. **Binary search** — `rdmDiscRange()` (`rdm_disc.cpp:73`) initializes a stack with `[0, RDM_UID_MAX]` (`rdm_task.cpp:41` passes `0` and `uidPack(RDM_UID_MAX)` at `rdm_disc.h:54`).
4. **Branch probe** — for each range, `rdmDiscBranch()` (`rdm_disc.cpp:16`) builds a 12-byte parameter payload (lower UID + upper UID, `rdm_disc.cpp:17-19`), transmits a broadcast `DISC_UNIQUE_BRANCH` request via `rdmBuild()` + `rdmTx()` (`rdm_disc.cpp:23-25`), and reads the UART for up to `RDM_DISC_TIMEOUT_MS` (6 ms, `rdm_engine.h:20`) via `uartRxRead()` (`rdm_disc.cpp:27`).
5. **Collision parsing** — if bytes were received, the parser scans for the `0xAA` separator byte preceded by `0xFE` fill bytes (`rdm_disc.cpp:32-35`), extracts the 6 UID bytes by AND-ing paired response bytes (`rdm_disc.cpp:39`), and validates the 2-byte complement checksum (`rdm_disc.cpp:40-42`).
6. **Branch resolution** — returns `0` (empty, `rdm_disc.cpp:49`), `1` (one UID found, `rdm_disc.cpp:45`), or `2` (collision).
7. **Mute + record** — on a single-UID result, `rdmMute()` (`rdm_disc.cpp:52`) sends `DISC_MUTE` and waits for ACK (`rdm_disc.cpp:54-61`); duplicates are filtered (`rdm_disc.cpp:86-88`) before appending to the output array (`rdm_disc.cpp:89`).
8. **Range split** — on collision, the range is bisected at the midpoint and both halves are pushed onto the static stack (`rdm_disc.cpp:93-96`).
9. **Budget** — the search is bounded by `8 * max + 128` iterations (`rdm_disc.cpp:76`), preventing unbounded recursion.

## 9. Protocol Layout

### DISC_UNIQUE_BRANCH request parameter data (`rdm_disc.cpp:17-19`)

| Offset | Size | Field |
|---|---|---|
| 0–5 | 6 | Lower-bound UID (big-endian, via `putUid`) |
| 6–11 | 6 | Upper-bound UID (big-endian, via `putUid`) |

Total: 12 bytes. Sent as a broadcast command to `RDM_UID_BROADCAST_ALL` (`rdm_engine.h:50`).

### DISC_UNIQUE_BRANCH response format (`rdm_disc.cpp:32-46`)

| Byte pattern | Meaning |
|---|---|
| `0xFE` × N | Fill bytes (responders drive 0xFE on each UID bit position). |
| `0xAA` | Separator marking start of encoded UID. |
| 6 bytes of pairs | Each UID byte is sent as two nibbles AND-ed together: `u[i] = e[i*2] & e[i*2+1]` (`rdm_disc.cpp:39`). |
| 2 bytes | Complement checksum: each UID byte + 0xFF summed, compared to the 2-byte trailing checksum (`rdm_disc.cpp:40-42`). |

### DISC_MUTE / DISC_UN_MUTE

No parameter data (`rdm_engine.cpp:88` with `pdl=0`). Transmitted as a broadcast for UN_MUTE (`rdm_disc.cpp:67`) and individually addressed for MUTE (`rdm_disc.cpp:56`).

## 10. Config Integration

No `Config` fields are read directly by this module. The responder-count ceiling `cfg.rdmMaxDev` is applied by the blocking wrapper `rdmRmtDiscover()` in [core-rdm-task](./core-rdm-task.md) (`rdm_task.cpp:323`), which clamps the `max` argument before passing it to `rdmDiscoverAsync()`.

## 11. Lifecycle

| Phase | Function | Line | What happens |
|---|---|---|---|
| **Begin sweep** | `rdmUnMuteAll` | `rdm_disc.cpp:64` | Broadcast `DISC_UN_MUTE`, 1 ms delay, UART flush. |
| **Probe range** | `rdmDiscRange` | `rdm_disc.cpp:73` | Iterative binary search with budget cap. |
| **Single probe** | `rdmDiscBranch` | `rdm_disc.cpp:16` | Single `DISC_UNIQUE_BRANCH` request + 3 retry attempts. |
| **Mute device** | `rdmMute` | `rdm_disc.cpp:52` | Up to 3 attempts to `DISC_MUTE` a found UID. |
| **End** | `rdmRmtDiscover` | `rdm_task.cpp:320` | Waits on a binary semaphore with a 30-second budget (`rdm_task.cpp:334`). |

## 12. Error Handling

| Function | Return type | Failure condition | Line |
|---|---|---|---|
| `rdmDiscBranch` | `int` | No bytes received after 3 attempts (returns 0, "empty") | `rdm_disc.cpp:49` |
| `rdmDiscBranch` | `int` | Bytes received but no valid UID extracted (returns 2, "collision") | `rdm_disc.cpp:49` |
| `rdmMute` | `bool` | No ACK after 3 attempts (returns false) | `rdm_disc.cpp:61` |
| `rdmRmtDiscover` | `int` | Task/queue down or 30-second timeout (returns 0) | `rdm_task.cpp:326,336` |
| `rdmDiscRange` | `void` | Budget exhausted (`budget--` hits 0) | `rdm_disc.cpp:77` |

No logging is performed; callers interpret the return values.

## 13. Allocation

All buffers are statically allocated or stack-local:

- `stkLo[64]` and `stkHi[64]` — `static uint64_t` arrays at `rdm_disc.cpp:74`, allocated in DRAM.
- `pd[12]`, `pkt[64]`, `rx[48]` — stack-local inside `rdmDiscBranch()` (`rdm_disc.cpp:17,22,26`).
- `resp[8]` — stack-local inside `rdmMute()` (`rdm_disc.cpp:53`).
- No heap or PSRAM allocation.

## 14. Timing

| Constraint | Value | Source |
|---|---|---|
| Per-branch UART read timeout | 6 ms | `rdm_engine.h:20` (`RDM_DISC_TIMEOUT_MS`) |
| Per-branch retry attempts | 3 | `rdm_disc.cpp:21` |
| Inter-attempt delay | 200 µs | `rdm_disc.cpp:29` |
| Mute retry delay | 1 ms | `rdm_disc.cpp:59` |
| Unmute-to-sweep delay | 1 ms | `rdm_disc.cpp:69` |
| Total discovery budget | 30 seconds | `rdm_task.cpp:334` |
| Search iteration budget | `8 * max + 128` | `rdm_disc.cpp:76` |

## 15. Traceability

| Claim | Evidence |
|---|---|
| `uidPack` shifts man_id to the upper 32 bits. | `rdm_disc.cpp:9` |
| `uidUnpack` reverses the 64-bit pack. | `rdm_disc.cpp:13` |
| `rdmDiscBranch` sends broadcast DISC_UNIQUE_BRANCH. | `rdm_disc.cpp:23-24` |
| Branch payload is 12 bytes (lower + upper UID). | `rdm_disc.cpp:17` |
| Response parser scans for `0xAA` separator. | `rdm_disc.cpp:33` |
| UID bytes are AND-ed from paired response bytes. | `rdm_disc.cpp:39` |
| Checksum is complement-based (byte + 0xFF). | `rdm_disc.cpp:40` |
| Return 0 = empty, 1 = found, 2 = collision. | `rdm_disc.h:12` |
| `rdmMute` retries up to 3 times with 1 ms delay. | `rdm_disc.cpp:54,59` |
| `rdmUnMuteAll` transmits broadcast, delays 1 ms, flushes UART. | `rdm_disc.cpp:68-70` |
| `rdmDiscRange` uses static stack arrays of depth 64. | `rdm_disc.cpp:74` |
| Budget formula is `8 * max + 128`. | `rdm_disc.cpp:76` |
| Range is bisected at the midpoint on collision. | `rdm_disc.cpp:94` |
| `rdmRmtDiscover` is implemented in `rdm_task.cpp`, not `rdm_disc.cpp`. | `rdm_disc.cpp:101-104` |
| `rdmRmtDiscover` applies the `cfg.rdmMaxDev` ceiling. | `rdm_task.cpp:323` |
| Discovery runs on core 1 at priority 18. | `rdm_disc.cpp:102` |
| Low-level primitives are called directly on the task thread. | `rdm_disc.cpp:104` |

## 16. Cross-References

- **[core-rdm-engine](./core-rdm-engine.md)** — provides `rdmBuild()`, `rdmTx()`, `rdmReadResp()`, `uartRxRead()`, `gpioDeSet()`, and `g_rdm` state; declared as "used by rdm_disc.cpp too" at `rdm_engine.h:62`.
- **[core-rdm-task](./core-rdm-task.md)** — implements `rdmRmtDiscover()` (blocking wrapper at `rdm_task.cpp:320`) and dispatches `RDM_CMD_DISCOVER` to the core-1 task.
- **[core-rdm-typed](./core-rdm-typed.md)** — not directly related; typed wrappers operate on individual PIDs, not discovery.
- **[include-headers](./include-headers.md)** — documents `rdm_types.h` constants (`RDM_UID_BROADCAST_ALL`, `RDM_UID_MAX`, `RDM_PID_DISC_UNIQUE_BRANCH`, `RDM_PID_DISC_MUTE`, `RDM_PID_DISC_UN_MUTE`).
- **[sys-tasks](./sys-tasks.md)** — `createTasks()` spawns the RDM task that runs discovery on core 1.

## 17. Limitations

- **Stack depth limit** — the static stack arrays cap the binary search recursion at 64 levels (`rdm_disc.cpp:74`). If more than 64 nested range splits are needed, deeper ranges are silently dropped.
- **Budget is heuristic** — the `8 * max + 128` iteration budget (`rdm_disc.cpp:76`) is not a time-bounded guarantee; a large responder population with many collisions could exceed the 30-second semaphore timeout (`rdm_task.cpp:334`).
- **No duplicate UID within a single sweep** — duplicates are deduplicated (`rdm_disc.cpp:86-88`) but only against the current `out[]` array; if the same UID appears in two sibling branches (unlikely but spec-permitted for misbehaving responders), it could be recorded twice.
- **Collision detection is byte-based** — the parser treats any received bytes that fail checksum validation as a "collision" (return 2, `rdm_disc.cpp:49`), which could falsely trigger range splitting on a noise-corrupted single-responder response.

## 18. Open Questions

- Not determinable from the inspected source code — whether the 64-level stack depth is sufficient for the maximum expected responder count on the LuxDMX-4uni board (4 RDM-capable universes).
- Not determinable from the inspected source code — the exact behavior when `rdmMute()` fails for a discovered device: the device remains in the UID list but is not muted, potentially appearing in future branch sweeps.
- Not determinable from the inspected source code — whether `rdmDiscRange` is ever called with a non-zero `lo0` other than 0 (the only call site is `rdm_task.cpp:41`).

## 19. Testing

- No dedicated unit tests exist for `rdm_disc.cpp` primitives (`rdmDiscBranch`, `rdmMute`, `rdmUnMuteAll`, `rdmDiscRange`, `uidPack`, `uidUnpack`), even though `rdm_types_test.cpp` and `test/unit-test/test_rdm_types/test_unit_rdm_types.cpp` cover the `rdm_uid_t` struct used by these functions.
- No hardware-in-loop (HIL) test scripts were found in the inspected source tree for the discovery algorithm.

## 20. History

- `rdmRmtDiscover()` was extracted from `rdm_disc.cpp` into `rdm_task.cpp` so that the full discovery sweep dispatches to the dedicated core-1 RDM task, preventing blocking of the DMX TX task — `rdm_disc.cpp:101-104`.
- Low-level primitives (`rdmDiscBranch`, `rdmMute`, `rdmUnMuteAll`, `rdmDiscRange`) were kept in `rdm_disc.cpp` to remain callable directly on the task thread without an async dispatch round-trip — `rdm_disc.cpp:104`.
