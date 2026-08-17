# Crash Guard â€” System Specification

Domain: sys.crash-guard

## 1. Module Overview

The Crash Guard is an NVS-backed crash-recovery mechanism that prevents a bad DMX output pin configuration from bricking the device. On every boot it reads a one-byte counter from the `"dmxgw"` NVS namespace; if the counter is non-zero, indicating a prior crash during output initialization, it progressively disables outputs before initialization runs. If the device survives a 3-second stability window after output init, the counter is reset to zero. If a panic occurs during the window, the counter persists and increments, causing the next boot to disable one more output.

**Owns:** the NVS crash counter (`dmxcrash` key in the `dmxgw` namespace), the 3-second stability window, the progressive output-disable logic.
**Delegates to:** Output Initialization (the wrapped init function), Config Engine (mutating `cfg.outputs[i].enabled` in RAM).
**Consumed by:** System bring-up (`setup`), Task Scheduling (`dmxTxTask` reads the resulting `outReady[i]` / `cfg.outputs[i].enabled` pair).

## 2. External Interfaces

### NVS Crash Counter

| Key | Namespace | Type | Default | Description |
|---|---|---|---|---|
| dmxcrash | "dmxgw" | uint8 | 0 (no crash remembered) | Incremented on each boot; reset to 0 on stable boot |

### Stability Window Constant

| Constant | Value | Unit |
|---|---|---|
| Guard TTL | 3000 | milliseconds |

### Guarded Init Sequence

The crash guard wraps exactly one function - the output initialization routine - with two calls from system bring-up:

| Call | Position | Purpose |
|---|---|---|
| Guard begin | Before output init | Read counter; disable outputs if recovering |
| Guard end | After output init | Write incremented counter; wait 3 s; reset on stable |

### Output Disable Range

On crash recovery (counter > 0), outputs in the range `[0 .. counter-1]` are disabled, clamped to `MAX_OUTPUTS`. The loop iterates from the highest index in the range down to zero. This means:
- First crash (counter = 1): output 0 is disabled.
- Second crash (counter = 2): outputs 0 and 1 are disabled.
- Third crash (counter = 3): outputs 0, 1, and 2 are disabled.

Each successive crash cycle adds the next higher-indexed output to the disabled set, starting from index 0.

## 3. State Machine

The crash guard is a 3-state NVS-backed machine:

```
        counter==0                 counter>0
   +----------+       +------------v------------+
   |   BOOT   |------>|   CRASH_RECOVERY        |
   | (read NVS)       | (disable outputs        |
   +----------+       |  [0..counter-1])        |
         | counter>0  +-----+-----------------+
         |                 | write counter+1
         v                 v
   +----------+     +----------+
   | INIT_OK  |     | WAIT_3S  |
   | (no dis) |     | (spin    |
   +----+-----+     | 3000ms)  |
        | survives 3s?  +---+----+
        |               |    |     |
        v              YES   v    NO
   +----------+     +----------+   (counter stays >0,
   | STABLE   |     | STABLE   |   next boot disables more)
   | _BOOT    |     | _BOOT    |
   | (count=0)|     | (count=0)|
   +----------+     +----------+
```

| Transition | Trigger | Action |
|---|---|---|
| BOOT -> INIT_OK | Counter == 0 at guard-begin | Proceed with all outputs enabled |
| BOOT -> CRASH_RECOVERY | Counter > 0 at guard-begin | Disable outputs `[0 .. counter-1]`, clamped by `MAX_OUTPUTS` |
| INIT_OK / CRASH_RECOVERY -> WAIT | Guard-end reached | Write `counter + 1` to NVS; begin 3 s stability wait |
| WAIT -> STABLE_BOOT | Survived 3 s (NVS value unchanged) | Reset counter to 0 |
| WAIT -> CRASH_RECOVERY (next boot) | Panic during the 3 s window | NVS value differs from written value; counter stays incremented |

## 4. Data Flow

1. **Read crash counter:** `guardBegin` opens the NVS namespace `"dmxgw"`, reads key `"dmxcrash"` with a default of `0`, and closes the namespace.
2. **Decide disable range:** If the crash counter is greater than zero, log the count, then loop from `counter - 1` down to `0` and set `cfg.outputs[i].enabled = false`, clamped by `MAX_OUTPUTS`.
3. **Init runs:** The output initialization routine executes, skipping any output marked disabled (`enabled == false`).
4. **Write incremented counter:** `guardEnd` opens `"dmxgw"`, writes `counter + 1` to `"dmxcrash"`, and closes the namespace.
5. **Stability wait:** Spin for 3 seconds (3000 ms) using 10 ms delay steps.
6. **Verify:** Re-open the NVS namespace, read `"dmxcrash"` with a fallback to the written value, and close.
7. **Reset on stable:** If the read value equals the written `counter + 1`, open the namespace again, write `0`, close, and log a stable-boot message.
8. **Crash during window:** If a panic occurs in steps 3-6, the NVS value is never reset to `0`. The next boot's `guardBegin` reads the incremented counter and disables one more output (step 2).

## 5. Configuration Integration

Reads:
- `cfg.outputs[i].enabled` - read by the output initialization routine to determine which outputs to initialize. The guard mutates this field in RAM during crash recovery.

Writes:
- `cfg.outputs[i].enabled = false` for outputs in the disabled range. This is a runtime, in-RAM mutation of the Config struct - it is **not** persisted to NVS configuration. On a stable reboot, config is reloaded from template + NVS overlay, re-enabling all outputs.
- NVS key `"dmxcrash"` in namespace `"dmxgw"` - the crash counter itself is the only persisted state.

The `enabled` field is a reboot-apply configuration field; the guard's in-RAM mutation is consistent with this - changes to `enabled` take effect only on the next init cycle, which the guard controls by mutating before `outputInitAll` runs.

## 6. Lifecycle

- **Init phase (core 0, setup):** `guardBegin()` then `outputInitAll()` then `guardEnd()`. The crash counter is read before output init; the incremented counter is written and the stability window enforced after output init.
- **Precondition:** The config engine must have loaded `cfg.outputs[i].enabled` (and all output pin fields) before the guard begins.
- **No periodic hook:** The guard runs exactly once per boot, before task scheduling begins.
- **No shutdown/cleanup:** The 3-second stability wait is the only "end" action; after it returns, setup proceeds to network bring-up, web server, and task creation.

## 7. Error Handling

| Condition | Handling |
|---|---|
| NVS read failure | Defaults to 0, so an unreadable or absent key is treated as "no crash remembered" (safe fallback: all outputs enabled) |
| NVS write failure | The verification read returns the old value; if it does not match `counter + 1`, the reset-to-zero block is skipped, leaving the counter elevated (conservative: next boot disables one more output) |
| 3-second stability wait | Uses `vTaskDelay(10)` for 10 ms steps. Since tasks are not yet spawned, only the idle task services the delay - no deadlock risk |
| Disable loop bounds | The loop is clamped to `i < MAX_OUTPUTS`; an oversized counter cannot index out of bounds |
| Counter overflow | The counter is a `uint8_t`; after 255 crash cycles it wraps to 0, re-enabling all outputs. This is theoretical (about 7.6 hours of continuous reboot cycling at 3 s/boot) and not a practical concern |

## 8. Timing Constraints

| Operation | Value |
|---|---|
| Stability window | 3000 ms (hard boot-time delay) |
| Wait granularity | 10 ms per step |
| NVS read (begin + get + end) | Sub-millisecond, single operation |
| NVS write (begin + put + end) | Sub-millisecond, single operation |
| Disable loop | O(crashCount) <= O(MAX_OUTPUTS), negligible |
| Boot delay | `setup()` is held for 3 seconds at `guardEnd` before tasks spawn |

The 3-second stability window is a hard boot-time delay: `setup()` cannot proceed to task creation until the guard-end function returns. This is by design - a stable 3-second gap after output initialization proves the RMT driver and output pins did not panic.

## 9. Memory and Allocation Model

- **Crash counter:** Static file-scope `uint8_t` in BSS (zero-initialized).
- **NVS namespace string:** Static const string in rodata.
- **Guard TTL constant:** Static const `uint32_t` in rodata.
- **Preferences objects:** Stack-local NVS wrapper objects, opened and closed per call with no retained allocation.
- No heap allocation in the guard. NVS uses its own internal flash partition; the guard's stack objects do not touch it.

## 10. Safety Considerations

- **Core isolation:** The crash guard runs on core 0 during `setup()`, before any task is spawned. It does not interfere with the core-1 DMX transmit task, which does not yet exist during the guard window.
- **Brick prevention:** A faulty output pin or RMT channel configuration that causes a panic during `outputInitAll` is progressively isolated. On the first crash, output 0 is disabled; on each subsequent crash, one more output is disabled (by increasing index), eventually isolating the faulty pin.
- **Ephemeral mutation:** The guard disables outputs only in RAM. A surviving boot reloads config from NVS (all outputs enabled), so a one-off crash does not permanently disable outputs - only repeated crash cycles trigger progressive disablement.
- **Conservative failure mode:** If the NVS write of the incremented counter fails (flash wear, power loss during write), the verification read detects the mismatch and leaves the counter elevated, causing the next boot to disable more outputs. This errs on the side of caution.
- **Safe fallback on NVS corruption:** If the NVS namespace is corrupted or unreadable, the default value of 0 is returned, meaning "no crash" - all outputs remain enabled. The device is not bricked; it simply retries init fully.
- **Concurrency:** The entire guard executes single-threaded on core 0 during `setup()`, before any FreeRTOS task is created. The 3-second stability wait uses `vTaskDelay` with only the idle task to service it - no contention. No locks, seqlocks, or SPSC rings are involved.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| Output Init | downstream (wrapped) | The output initialization routine is the init function executed between guard-begin and guard-end; reads `cfg.outputs[i].enabled` |
| Config Engine | upstream (consumed) | Loads `cfg.outputs[i].enabled` before the guard runs; the guard mutates this field in RAM |
| Task Scheduling | downstream | `dmxTxTask` reads the `outReady[i]` / `cfg.outputs[i].enabled` pair that the guard ensures is valid |
| NVS / Preferences | internal | ESP32 Arduino NVS wrapper provides the persistent key-value store for the crash counter |

## 12. Testing Verification

No host-native test covers the crash guard - the guard uses NVS (`Preferences`), which the native test shims do not emulate. The guard is exercised by inducing a panic during output initialization and observing progressive output disablement across reboots - a manual hardware test. No automated soak test triggers the counter; the soak monitor only logs heap statistics and reboots on low DRAM, without interacting with the crash counter.

## 13. Open Questions

1. Whether there is a separate code path (e.g., a web `/reset` handler or the config-save reboot flow) that also increments the crash counter, or whether it is managed exclusively by the guard.
2. Whether the 3-second stability window value is tunable per-board or hardcoded across all environments (no build-flag override was found).
3. Whether the output initialization function performs any internal NVS read that could race with the guard's in-RAM mutation of `cfg.outputs[i].enabled`.

## 14. History

- Crash guard moved into the tasks module during the 5-layer refactor; previously part of the output-init module.
- NVS namespace `"dmxgw"` is shared with other sys-layer NVS keys.
- Progressive disable-by-lowest-index heuristic ensures output at index 0 is disabled on the first crash, with each successive crash adding the next higher index.
- 3-second stability window chosen to span at least one full RMT priming frame (~92 ms worst case for 4 outputs) plus WiFi association latency, ensuring a stable boot window before task scheduling begins.
- The guard mutates `cfg.outputs[i].enabled` in RAM rather than persisting a "disabled-by-crash" bit in NVS - an intentional design so a clean reboot re-enables all outputs.
