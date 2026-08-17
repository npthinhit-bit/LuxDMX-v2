# RDM Discovery - System Specification

## 1. Module Overview

**Module ID:** core.rdm_disc
**Domain:** E1.20 DISC_UNIQUE_BRANCH binary-search device discovery
**Layer:** core (calls core.rdm_engine transport primitives, runs on core-1 RDM task)

Implements the E1.20 discovery protocol for enumerating RDM responders on a DMX line:

- **DISC_UNIQUE_BRANCH** (0x0001): Binary-search collision discovery across the 48-bit UID space.
- **DISC_MUTE** (0x0002): Quells an individual responder so it drops out of subsequent sweeps.
- **DISC_UN_MUTE** (0x0003): Broadcasts to clear all muted responders before a new search.

The module packs/unpacks 6-byte UIDs into a sortable 64-bit integer, sends a single broadcast branch probe, parses the fill-byte + separator + AND-ed UID + complement checksum response, and runs an iterative binary search that bisects any range returning a collision until every responder is found and muted.

All primitives execute on core 1 inside the RDM task, which is the sole owner of the active RMT channel and RX UART -- no mutex is required.

### UID Space

| Bound | Value | Encoding |
|---|---|---|
| RDM_UID_MIN | 0x000000000000 | man_id = 0, dev_id = 0 |
| RDM_UID_MAX | 0xFFFFFFFFFFFF | man_id = 0xFFFF, dev_id = 0xFFFFFFFF |
| Packing | 64-bit integer | man_id (16-bit) in upper 32 bits; dev_id (32-bit) in lower 32 bits |

## 2. External Interfaces

### Entry Points

| Function | Caller | Trigger | Core |
|---|---|---|---|
| rdmRmtDiscover() | WebSocket handler, Art-Net bridge (via blocking task wrapper) | External "discover" command | Core 0 -> core 1 (blocking) |
| rdmDiscRange(lo, hi, max, out[], outN) | RDM task (RDM_CMD_DISCOVER) | Discovery job dispatched | Core 1 |
| rdmUnMuteAll() | RDM task | Start of discovery sweep | Core 1 |
| rdmMute(uid, max) | RDM task | Mute a single discovered device | Core 1 |

### Input (read-only)

- g_rdm state: active UART port, DE/RE GPIO pin, active RMT handle -- for transmitting broadcast requests and reading collision responses.
- Lower and upper UID bounds for each search range (packed as 6-byte big-endian values in the 12-byte parameter payload).
- cfg.rdmMaxDev -- ceiling on the number of responders to collect (applied by the blocking wrapper before dispatch).

### Output (written)

| Target | What | Core |
|---|---|---|
| RMT peripheral | DISC_UNIQUE_BRANCH / DISC_MUTE / DISC_UN_MUTE request symbols | Core 1 |
| DE/RE GPIO | Direction state per request | Core 1 |
| Result array | Discovered responder UIDs | Core 1 (not crossed to core 0 mid-sweep) |

## 3. State Machine

**No state machine** -- the binary search is a single iterative loop. It maintains an explicit stack of UID-range pairs and a per-call iteration budget. No persistent discovery state survives a sweep.

Per-branch result handling:

- **Return 0 (empty)**: Range contains no responders; pop the next range from the stack.
- **Return 1 (one UID found)**: Attempt to mute the device; if it is not already recorded, append it to the output array. Continue sweeping the same range (the muted responder no longer answers).
- **Return 2 (collision)**: Split the range at the midpoint and push both halves onto the stack for depth-first probing.

Each invocation of rdmRmtDiscover() starts fresh with a broadcast DISC_UN_MUTE to reset all responder mute state.

## 4. Data Flow

1. **Dispatch**: Core-0 code (WebSocket handler or Art-Net bridge) calls the blocking rdmRmtDiscover() wrapper, which enqueues a RDM_CMD_DISCOVER command to the core-1 RDM task queue.

2. **Initial un-mute**: The task handler calls rdmUnMuteAll(), which builds a broadcast DISC_UN_MUTE request, transmits it via RMT, delays 1 ms, and flushes the RX UART -- ensuring all responders are audible before the sweep.

3. **Binary search entry**: The search initializes a stack with the full range [0, RDM_UID_MAX].

4. **Branch probe**: For each range on the stack, rdmDiscBranch() builds a 12-byte parameter payload (lower-bound UID + upper-bound UID, each 6 bytes big-endian), transmits a broadcast DISC_UNIQUE_BRANCH request, and reads the UART for up to 6 ms.

5. **Collision parsing**: If bytes were received, the parser scans for the 0xAA separator byte preceded by 0xFE fill bytes, extracts the 6 UID bytes by AND-ing paired response bytes (each UID byte is transmitted as two nibbles), and validates the 2-byte complement checksum (each UID byte + 0xFF summed against the trailing 2 bytes).

6. **Branch resolution**: Returns 0 (empty -- no bytes received after retries), 1 (one UID found -- single valid checksum match), or 2 (collision -- multiple responders answered or checksum failed on a non-empty response).

7. **Mute + record**: On a single-UID result, rdmMute() sends DISC_MUTE to that UID and waits for an ACK. Duplicates are filtered against the current result array before appending.

8. **Range split**: On collision, the range is bisected at the midpoint and both halves are pushed onto the static stack.

9. **Budget enforcement**: The search is bounded by 8 * max + 128 iterations, preventing unbounded recursion.

## 5. Configuration Integration

No Config fields are read directly by this module. The responder-count ceiling cfg.rdmMaxDev is applied by the blocking wrapper before dispatch, clamping the maximum number of UIDs to collect.

## 6. Lifecycle

- **Begin sweep**: rdmUnMuteAll() broadcasts DISC_UN_MUTE, delays 1 ms, flushes UART.
- **Probe range**: rdmDiscRange() runs the iterative binary search with budget cap.
- **Single probe**: rdmDiscBranch() sends one DISC_UNIQUE_BRANCH request with 3 retry attempts.
- **Mute device**: rdmMute() sends DISC_MUTE with up to 3 attempts.
- **End**: rdmRmtDiscover() waits on a binary semaphore with a 30-second total timeout budget.

## 7. Error Handling

| Function | Return | Failure condition |
|---|---|---|
| rdmDiscBranch | int (0/1/2) | No bytes received after 3 attempts -> 0 (empty); bytes received but no valid UID -> 2 (collision) |
| rdmMute | bool | No ACK after 3 attempts -> false |
| rdmRmtDiscover | int | Task/queue down OR 30-second timeout -> 0 |
| rdmDiscRange | void | Iteration budget exhausted (budget counter reaches 0) |

No logging is performed; callers interpret the return values. Discovery results are never crossed to core 0 mid-sweep -- they remain on core 1 until the semaphore signals completion.

## 8. Timing Constraints

| Constraint | Value | Core |
|---|---|---|
| Per-branch UART read timeout | 6 ms | Core 1 |
| Per-branch retry attempts | 3 | Core 1 |
| Inter-attempt delay | 200 us | Core 1 |
| Mute retry delay | 1 ms | Core 1 |
| Unmute-to-sweep delay | 1 ms | Core 1 |
| Total discovery budget | 30 seconds | Core 1 (semaphore timeout) |
| Search iteration budget | 8 * max + 128 | Core 1 |

## 9. Memory and Allocation Model

All buffers are statically allocated or stack-local -- no heap or PSRAM allocation.

- **UID range stacks**: static uint64_t[64] arrays (lower-bound and upper-bound), one entry per search depth, in DRAM.
- **Branch payload**: 12-byte stack buffer for the lower + upper UID.
- **Build buffer**: 64-byte stack buffer for the request packet.
- **RX buffer**: 48-byte stack buffer for the collision response.
- **Mute response**: 8-byte stack buffer.

No PSRAM or heap calls occur. The static qualifier on the stack arrays means they persist across calls within the task context -- the single-threaded RDM task design makes re-entrancy protection unnecessary.

## 10. Safety Considerations

- **Core-1 isolation**: All discovery primitives execute on the dedicated RDM task (priority 18). The RMT and UART are owned exclusively by this task, eliminating concurrent-access races.
- **No cross-core response queue**: Discovery results stay on core 1; they are never published to the response ring used by async Art-Net RDM traffic. This prevents mid-sweep interruption by other RDM transactions.
- **Budget cap**: The 8 * max + 128 iteration budget and the 30-second semaphore timeout prevent indefinite blocking of the DMX TX task.
- **Stack depth limit**: The 64-level static stack bounds recursion depth; deeper ranges are silently dropped rather than overflowing DRAM.
- **Checksum validation**: The complement checksum on each branch response prevents misparsed noise from being recorded as a phantom responder.
- **Duplicate filtering**: Discovered UIDs are deduplicated against the current result array, preventing the same device from consuming multiple result slots.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| core.rdm-engine | calls rdmBuild / rdmTx / rdmReadResp / uartRxRead / gpioDeSet | RMT transmit, UART receive, DE/RE GPIO for broadcast requests and mute commands |
| core.rdm-task | dispatches RDM_CMD_DISCOVER; implements rdmRmtDiscover blocking wrapper | Job scheduling, semaphore with 30-second timeout |
| cfg (config_schema) | read (by task wrapper) | rdmMaxDev ceiling clamped before dispatch |
| include.rdm-types | read | rdm_uid_t, RDM_UID_BROADCAST_ALL, RDM_UID_MAX, PID constants (DISC_UNIQUE_BRANCH, DISC_MUTE, DISC_UN_MUTE) |
| drv.dmx-rmt-tx | (transitive via rdm-engine) | RMT symbol encoding for broadcast requests |
| drv.dmx-uart-rx | (transitive via rdm-engine) | UART RX for collision responses |
| drv.gpio-dir | (transitive via rdm-engine) | DE/RE direction control |

## 12. Testing Verification

| Test Case | File | Validates |
|---|---|---|
| UID pack/unpack | rdm_types_test / unit-test/test_rdm_types | 48-bit UID struct layout, constants (RDM_UID_BROADCAST_ALL, RDM_UID_MAX, DISC_PID constants) |

**Untested**: No unit tests exist for rdmDiscBranch, rdmMute, rdmUnMuteAll, rdmDiscRange, uidPack, or uidUnpack. No hardware-in-loop test scripts were found for the discovery algorithm. Collision parsing, checksum validation, midpoint bisection, retry logic, and the budget cap are validated only on hardware with live responder populations.

## 13. Open Questions

1. Whether the 64-level static stack depth is sufficient for the maximum expected responder count on the LuxDMX-4uni board (4 RDM-capable universes).
2. The exact behavior when rdmMute() fails for a newly discovered device -- whether the device remains in the UID list unmuted and how this affects subsequent sweeps.
3. Whether rdmDiscRange is ever called with a non-zero lower bound other than 0 (the only known call site starts the search at UID 0).

## 14. History

- **Task dispatch extraction**: rdmRmtDiscover() was extracted from the discovery module into the core-1 RDM task so that a full discovery sweep dispatches to the dedicated task, preventing blocking of the DMX TX task.
- **Low-level primitive retention**: rdmDiscBranch, rdmMute, rdmUnMuteAll, and rdmDiscRange were kept in the discovery module so they remain callable directly on the task thread without an async dispatch round-trip.
- **Binary search algorithm**: E1.20 DISC_UNIQUE_BRANCH binary search with static 64-deep stack arrays and an 8 * max + 128 iteration budget.
- **30-second total budget**: Enforced by the blocking wrapper's semaphore timeout to cap worst-case discovery time for dense responder topologies.