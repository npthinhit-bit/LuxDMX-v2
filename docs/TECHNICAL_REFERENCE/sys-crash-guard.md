# Crash Guard — Technical Reference

Domain: sys.crash-guard

## 1. Domain Scope

Owns the NVS-backed crash-recovery mechanism that prevents a bad DMX output pin from bricking the device. On every boot it reads a one-byte counter from the `"dmxgw"` Preferences namespace; if the counter is non-zero (indicating a previous crash during `outputInitAll`), it progressively disables the lowest-indexed outputs before init runs. If the device survives a 3-second stability window, the counter is reset to zero; otherwise it persists and the next boot disables one more output.

The guard wraps `outputInitAll()` — see `[core-output-init](./core-output-init.md)` for the full init sequence. The runtime output table it protects (`g_outputs[]`, `outReady[]`) lives in `[core-output-init](./core-output-init.md)` and is consumed by `[sys-tasks](./sys-tasks.md)` (`dmxTxTask`).

This module owns: `dmxInitGuardBegin()` and `dmxInitGuardEnd()` — both in `src/sys/tasks.cpp`.

## 2. Layer Mapping

```
drv  →  cfg  →  core  →  net  →  app/sys
                      │          ↑
                      │   NVS (Preferences,
                      │    "dmxgw" namespace)
                      │          │
                      ↓          │
              outputInitAll() ←──┘
              (core-output-init)
              dmxInitGuardBegin/End
              (sys/tasks.cpp)
```

The crash guard is a **sys**-layer mechanism (`src/sys/tasks.cpp:43-76`) that protects a **core**-layer init function (`outputInitAll`, `src/core/output_init.cpp:35`) by mutating a **cfg**-layer struct field (`cfg.outputs[i].enabled`, set `false` at `src/sys/tasks.cpp:52`). The NVS persistence itself is **sys/app** boundary (`Preferences.h`, the ESP32 Arduino wrapper over NVS).

## 3. Source Files

| File | Role |
|---|---|
| `src/sys/tasks.cpp` | `dmxInitGuardBegin` (line 43), `dmxInitGuardEnd` (line 57), `s_crashCount` (line 39), `PREF_NS_TASKS` (line 40), `DMX_GUARD_TTL_MS` (line 41) |
| `src/sys/tasks.h` | `dmxInitGuardBegin` (line 11), `dmxInitGuardEnd` (line 12) declarations |
| `include/output.h` | `dmx_output_t::ready` field (line 25) — the per-output "RMT init succeeded" flag the guard indirectly protects via `outReady[]` ([core-output-init](./core-output-init.md)) |
| `src/core/output_init.cpp` | `outputInitAll()` (line 35) — the init function the guard wraps; reads `cfg.outputs[i].enabled` (line 49) which the guard mutates |
| `src/core/output_init.h` | `outReady[]` declaration (line 6) — read by `dmxTxTask` after guard passes |
| `src/main.cpp` | Call sequence: `dmxInitGuardBegin` → `outputInitAll` → `dmxInitGuardEnd` (lines 106-108) |
| `include/config_schema.h` | `MAX_OUTPUTS` constant (line 9) — bounds the disable loop |
| `include/config_enums.h` | Not directly used by the guard. |

## 4. Data Structures

### `s_crashCount` (`src/sys/tasks.cpp:39`)

| Field | Type | Initial | Description |
|---|---|---|---|
| `s_crashCount` | `static uint8_t` | `0` | NVS-persisted crash counter. Read at guard-begin, incremented and written at guard-end. |

### NVS keys (`src/sys/tasks.cpp:40,46,60,67,72`)

| Key | Namespace | Type (Preferences) | Reset value |
|---|---|---|---|
| `dmxcrash` | `"dmxgw"` | `UChar` (uint8) | `0` (no crash remembered) |

### `DMX_GUARD_TTL_MS` (`src/sys/tasks.cpp:41`)

| Constant | Value | Unit |
|---|---|---|
| `DMX_GUARD_TTL_MS` | `3000` | milliseconds |

### `include/output.h` references (read-only consumed by the guarded path)

The `ready` field (`dmx_output_t::ready`, `include/output.h:25`) and its companion `outReady[i]` (`src/sys/tasks.cpp:97,128` — see [sys-tasks](./sys-tasks.md)) are the per-output success flags whose validity the crash guard ensures. They are not written by the guard itself.

## 5. Concurrency

**Single-threaded (core 0, `setup()` only).** `dmxInitGuardBegin` and `dmxInitGuardEnd` both execute during `setup()` on core 0, before `createTasks()` spawns any FreeRTOS task (`src/main.cpp:106-130`). The 3-second stability wait inside `dmxInitGuardEnd` uses `vTaskDelay(10)` (`src/sys/tasks.cpp:64`) — this is a FreeRTOS call, but only the main/setup thread is running at that point (tasks are not yet created), so `vTaskDelay` yields to the idle task with no contention. No locks, seqlocks, or SPSC rings are involved. `s_crashCount` is a plain `uint8_t` with no `volatile`/`atomic` qualification — safe because no concurrent reader exists during `setup()`.

## 6. State Machine

The crash guard is a 3-state NVS-backed machine:

```
        ┌─────────────┐  counter==0   ┌──────────────┐
        │   BOOT      │ ─────────────→ │   INIT_OK    │
        │ (read NVS)  │               │ (no disable) │
        └──────┬──────┘               └──────┬───────┘
               │ counter>0                  │
               ▼                            │
        ┌──────────────┐                    │ survives 3s
        │ CRASH_RECOVERY│                   │ (DMX_GUARD_TTL_MS)
        │ (disable      │                    ▼
        │  outputs)     │              ┌──────────────┐ reset
        └──────┬───────┘  panicked ┌───► │  STABLE_BOOT │ counter=0
               │ write           │     │ (counter=0)  │
               │ counter+1       │     └──────────────┘
               ▼                  │
         ┌───────────┐          survives 3s? ──NO──┐
         │ WAIT_3000 │ ──YES──► STABLE_BOOT      │
         │ (vTaskDelay) │                         ◄─┘ (counter stays >0,
         └───────────┘                            next boot disables more)
```

Transitions driven entirely by two functions:

| Transition | Trigger | Action | Code |
|---|---|---|---|
| BOOT → INIT_OK / CRASH_RECOVERY | `dmxInitGuardBegin` reads NVS | If counter == 0: proceed. If > 0: disable outputs `[0..counter-1]`. | `src/sys/tasks.cpp:43-55` |
| INIT_OK / CRASH_RECOVERY → WAIT_3000 | `dmxInitGuardEnd` | Write `counter + 1` to NVS; begin 3 s wait. | `src/sys/tasks.cpp:57-65` |
| WAIT_3000 → STABLE_BOOT | survived 3 s | NVS value still equals `counter+1` → reset to 0. | `src/sys/tasks.cpp:66-75` |
| WAIT_3000 → CRASH_RECOVERY (next boot) | panicked during 3 s | NVS value differs from written value → counter stays incremented; next boot reads it and disables more. | `src/sys/tasks.cpp:67-68` |

## 7. Entry Points

| Entry point | Address | Caller |
|---|---|---|
| `dmxInitGuardBegin()` | `src/sys/tasks.cpp:43` | `setup()` before `outputInitAll` (`src/main.cpp:106`) |
| `dmxInitGuardEnd()` | `src/sys/tasks.cpp:57` | `setup()` after `outputInitAll` (`src/main.cpp:108`) |
| (wrapped) `outputInitAll()` | `src/core/output_init.cpp:35` | Between the two guard calls (`src/main.cpp:107`) |

## 8. Data Flow

1. **Read crash counter** — `dmxInitGuardBegin` opens NVS namespace `"dmxgw"` (`src/sys/tasks.cpp:45`), reads key `"dmxcrash"` with default `0` (`src/sys/tasks.cpp:46`), closes (`src/sys/tasks.cpp:47`).
2. **Decide disable range** — if `s_crashCount > 0` (`src/sys/tasks.cpp:48`), log the count (`src/sys/tasks.cpp:49`), then loop `i` from `s_crashCount - 1` down to `0` and set `cfg.outputs[i].enabled = false` (`src/sys/tasks.cpp:50-53`), clamped by `i < MAX_OUTPUTS` (`src/sys/tasks.cpp:50`).
3. **Init runs** — `outputInitAll()` executes, skipping the disabled outputs (`src/core/output_init.cpp:49` checks `enabled`).
4. **Write incremented counter** — `dmxInitGuardEnd` opens `"dmxgw"` (`src/sys/tasks.cpp:59`), writes `s_crashCount + 1` to `"dmxcrash"` (`src/sys/tasks.cpp:60`), closes (`src/sys/tasks.cpp:61`).
5. **Stability wait** — spin `while (millis() - t0 < DMX_GUARD_TTL_MS)` with `vTaskDelay(10)` (`src/sys/tasks.cpp:62-65`).
6. **Verify** — re-open NVS (`src/sys/tasks.cpp:66`), read `"dmxcrash"` with fallback `s_crashCount + 1` (`src/sys/tasks.cpp:67`), close (`src/sys/tasks.cpp:68`).
7. **Reset on stable** — if the read value equals the written `s_crash_count + 1` (`src/sys/tasks.cpp:69`), open `"dmxgw"` again (`src/sys/tasks.cpp:70-71`), write `0` (`src/sys/tasks.cpp:72`), close, and log `"stable boot, crash counter reset"` (`src/sys/tasks.cpp:74`).
8. **Crash during window** — if a panic occurs in step 3–6, the NVS value is never reset to 0; the next boot's `dmxInitGuardBegin` reads the incremented counter and disables one more output (step 2).

## 9. Protocol Layout

N/A (no wire protocol). The crash guard communicates solely through the NVS key-value store and a mutation of `cfg.outputs[i].enabled`.

## 10. Config Integration

Reads/writes:
- **Writes** `cfg.outputs[i].enabled = false` for outputs in the disabled range (`src/sys/tasks.cpp:52`). This is a runtime mutation of the in-memory `Config` struct; it is **not** persisted to NVS (the crash counter itself is the persisted state). On reboot, config is reloaded from template + NVS overlay, so the disabled-by-guard change is intentionally ephemeral — a surviving boot re-enables all outputs.
- **Reads** `cfg.outputs[i]` indirectly via `outputInitAll` which checks `enabled` (`src/core/output_init.cpp:49`).

The `enabled` field is `CFG_REBOOT` in the schema (`src/cfg/config_schema.cpp:154`); the guard's mutation lives in the reboot-restricted band, consistent with the field's live-apply semantics (no live re-enable path exists, so disabling before init is correct).

## 11. Lifecycle

- **Init phase (core 0, `setup()`)**: `dmxInitGuardBegin()` → `outputInitAll()` → `dmxInitGuardEnd()` (`src/main.cpp:106-108`).
- **Precondition**: `cfgcore::load()` must have populated `cfg.outputs[i].enabled` before the guard begins (`src/main.cpp:46`).
- **No periodic hook** — the guard runs exactly once per boot, before `createTasks()`.
- **No shutdown/cleanup** — the 3-second wait is the only "end" action; after it returns, `setup()` proceeds to `syslogInit`, `initOTA`, `soakInit`, `rdmTaskInit`, network, web, and `createTasks()` (`src/main.cpp:109-130`).

## 12. Error Handling

- `dmxInitGuardBegin`: no return value; NVS failures are silently absorbed — `Preferences::getUChar` defaults to `0` if the key or namespace is absent (`src/sys/tasks.cpp:46`), so an NVS corruption that yields a read error simply means "no crash remembered" (safe fallback: all outputs enabled).
- `dmxInitGuardEnd`: NVS write failures are silently absorbed — if `putUChar` fails, the verification read at line 67 will return the old value; if it doesn't match `s_crashCount + 1`, the reset-to-0 block is skipped (`src/sys/tasks.cpp:69`) and the counter remains elevated (conservative: next boot disables more).
- `vTaskDelay(10)` inside the 3-second spinner (`src/sys/tasks.cpp:64`) — no error path; if the scheduler were somehow not running, this would hang the device in `setup()`, but tasks are not yet created so the idle task services the delay.
- The disable loop is clamped to `i < MAX_OUTPUTS` (`src/sys/tasks.cpp:50`) — an oversized counter cannot index out of bounds.

## 13. Memory Allocation

- `s_crashCount` — static file-scope `uint8_t` in `.bss` (`src/sys/tasks.cpp:39`).
- `PREF_NS_TASKS` — `static const char*` in `.rodata` (`src/sys/tasks.cpp:40`).
- `DMX_GUARD_TTL_MS` — `static const uint32_t` in `.rodata` (`src/sys/tasks.cpp:41`).
- `Preferences p` / `p2` — stack-local ESP32 Arduino NVS wrapper objects, opened/closed per call with no retained allocation (`src/sys/tasks.cpp:44-47,58-61,66-68,70-73`).
- No heap allocation in the guard. NVS uses its own internal partition (`partitions.csv` / `nvs`), untouched by this module's stack objects.

## 14. Timing

| Item | Value | Source |
|---|---|---|
| Stability window | 3000 ms | `src/sys/tasks.cpp:41` (`DMX_GUARD_TTL_MS`), waited at `src/sys/tasks.cpp:62-65` |
| Wait granularity | 10 ms per `vTaskDelay` | `src/sys/tasks.cpp:64` |
| NVS read (begin + getUChar + end) | single operation, sub-millisecond | `src/sys/tasks.cpp:44-47` |
| NVS write (begin + putUChar + end) | single operation, sub-millisecond | `src/sys/tasks.cpp:58-61` |
| Disable loop | O(s_crashCount) ≤ O(MAX_OUTPUTS=4), negligible | `src/sys/tasks.cpp:50` |
| Boot delay | `setup()` is held for 3 s at `dmxInitGuardEnd` before tasks spawn | `src/main.cpp:108` then `130` |

The 3-second window is a hard boot-time delay: `setup()` does not proceed to `createTasks()` (line 130) until the guard end returns. This is by design — a stable 3-second gap after output init proves the RMT driver did not panic.

## 15. Traceability

| Claim | Evidence |
|---|---|
| Crash counter is `uint8_t`, static, default 0 | `src/sys/tasks.cpp:39` |
| NVS namespace is `"dmxgw"` | `src/sys/tasks.cpp:40` |
| Guard TTL constant is 3000 ms | `src/sys/tasks.cpp:41` |
| `dmxInitGuardBegin` opens namespace, reads `dmxcrash`, closes | `src/sys/tasks.cpp:44-47` |
| Progressive disable: loop `i = crashCount-1` down to 0, clamped by `MAX_OUTPUTS` | `src/sys/tasks.cpp:50-53` |
| Disable action sets `cfg.outputs[i].enabled = false` | `src/sys/tasks.cpp:52` |
| `dmxInitGuardEnd` writes `crashCount + 1` | `src/sys/tasks.cpp:60` |
| 3-second spinner: `while (millis() - t0 < DMX_GUARD_TTL_MS) { vTaskDelay(10); }` | `src/sys/tasks.cpp:62-64` |
| Verification read with fallback to written value | `src/sys/tasks.cpp:67` |
| Reset-to-0 on match | `src/sys/tasks.cpp:69-75` |
| Counter left elevated on panic (no reset) | `src/sys/tasks.cpp:67-68` (read returns stale ≠ written) |
| Call site: `dmxInitGuardBegin` before `outputInitAll` | `src/main.cpp:106` |
| Call site: `outputInitAll` between guards | `src/main.cpp:107` |
| Call site: `dmxInitGuardEnd` after `outputInitAll` | `src/main.cpp:108` |
| `outputInitAll` skips disabled outputs | `src/core/output_init.cpp:49` |
| `MAX_OUTPUTS` bounds the loop | `src/sys/tasks.cpp:50`, declared `include/config_schema.h:9` |
| `outReady[]` / `g_outputs[]` read by `dmxTxTask` after guard | `src/sys/tasks.cpp:97,101,128` (see [sys-tasks](./sys-tasks.md)) |

## 16. Cross-References

- `[core-output-init](./core-output-init.md)` — owns `outputInitAll()` (the wrapped function) and `outReady[]`/`g_outputs[]` (the protected runtime table). Full crash-guard state-machine description also appears in that module's section 6.
- `[sys-tasks](./sys-tasks.md)` — `dmxTxTask` reads the `outReady[i]` / `cfg.outputs[i].enabled` pair that the guard mutates (`src/sys/tasks.cpp:97,128`).
- `config-engine` — `cfgcore::load()` populates `cfg.outputs[i].enabled` (and all output pin fields) before the guard runs (`src/main.cpp:46`); the guard's mutation is ephemeral and not written back to NVS config (only the crash counter is persisted).
- `[core-output-init](./core-output-init.md):9` — `include/output.h` defines `dmx_output_t::ready`, the success flag the guard path ultimately validates via `outReady`.

## 17. Limitations

- The guard reads and writes NVS **synchronously** in `setup()` — under a corrupted NVS partition this can add latency before boot proceeds, but `Preferences::begin` returns gracefully on namespace errors.
- The disable direction is **lowest-index first** (`i = crashCount-1` down to 0, `src/sys/tasks.cpp:50`) — this means output **A** (index 0) is disabled on the first crash. For multi-output rigs where output A is the most critical, this heuristic may be suboptimal; the index order is hardcoded, not config-driven.
- The 3-second stability window is a **fixed boot-time delay** with no early-out — `setup()` cannot proceed to networking or web server until it elapses (`src/main.cpp:108 → 130`), adding 3 s to every boot even on a healthy device.
- `s_crashCount` is a `uint8_t` — after 255 crash cycles it wraps to 0, re-enabling all outputs. At 1 crash/boot this would require ~7.6 hours of continuous reboot-cycling at 3 s/boot and is not a practical concern, but it is an unbounded counter.
- The guard mutates `cfg.outputs[i].enabled` in RAM only; if `outputInitAll` itself were to re-read NVS for `enabled` (it does not — it reads the in-memory `cfg`), the mutation would be lost. See [core-output-init](./core-output-init.md) which confirms `enabled` is read from `cfg` (`src/core/output_init.cpp:49,50`).

## 18. Open Questions

1. Not determinable from the inspected source code — whether there is a separate code path (e.g. a web `/reset` handler or the config-save reboot flow) that also increments the crash counter, or whether `dmxcrash` is managed exclusively by `dmxInitGuardBegin/End`.
2. Not determinable from the inspected source code — whether the 3-second window value (`DMX_GUARD_TTL_MS = 3000`) is tunable per-board or hardcoded across all environments; no `platformio.ini` `-D` override for `DMX_GUARD_TTL_MS` was found.
3. Not determinable from the inspected source code — whether `outputInitAll` performs any internal NVS read that could race with the guard's in-RAM mutation of `cfg.outputs[i].enabled`.

## 19. Testing

- No host-native test covers the crash guard — `dmxInitGuardBegin/End` use `Preferences` (NVS), which the `test/native/shim/` shims do not emulate.
- The guard is exercised by the manual hardware test described in [core-output-init](./core-output-init.md):19 — induce a panic during `outputInitAll`, reboot, and confirm progressive output disablement across reboots.
- No automated soak test triggers the counter — the `[sys-soak-monitor](./sys-soak-monitor.md)` module only logs heap stats and reboots on low-DRAM; it does not interact with `dmxcrash`.
- `config_test.cpp` does not reference `dmxcrash` or `DMX_GUARD_TTL_MS` (`test/native/config_test.cpp` covers only config field resolution).

## 20. History

- Crash guard moved to `src/sys/tasks.cpp` during the 5-layer refactor; previously part of the output-init module ([core-output-init](./core-output-init.md):20).
- NVS namespace `"dmxgw"` (`src/sys/tasks.cpp:40`) matches `PREF_NS` in `src/sys/sys_platform.cpp:4` — the namespace is shared with other sys-layer NVS keys.
- Progressive disable-by-lowest-index heuristic chosen over highest-index disable — see [Limitations](#17-limitations).
- 3-second stable window (`DMX_GUARD_TTL_MS`, `src/sys/tasks.cpp:41`) chosen to span at least one full RMT priming frame (92 ms worst case for 4 outputs per [core-output-init](./core-output-init.md):14) plus WiFi association latency.
- The guard mutates `cfg.outputs[i].enabled` (RAM) rather than persisting a "disabled-by-crash" bit in NVS — an intentional design so a clean reboot re-enables all outputs.
