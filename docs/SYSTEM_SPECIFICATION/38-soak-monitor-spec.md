# Soak Monitor Specification

Domain: sys.soak-monitor

## 1. Module Overview

The Soak Monitor is a diagnostic-only module that provides a long-term memory-integrity watchdog for the gateway. It runs a background task that logs DRAM and PSRAM free-heap statistics every 60 seconds and auto-reboots the device if free DRAM drops below 30 KB, indicating a memory leak or heap corruption. The entire module is conditionally compiled — it is only present when the soak-test build flag is defined, which is set exclusively for the 4-universe Ethernet build environment.

The module also exposes a JSON snapshot of current heap statistics over HTTP for remote monitoring during soak testing. It does not drive DMX, RDM, networking, or configuration — it is purely a diagnostic safety valve.

Owned: the soak-test task lifecycle, the DRAM low-water reboot threshold, the heap-statistics JSON endpoint.
Delegated: none.
Consumed by: System bring-up (init call during setup), Web routes (/diag/soak-stats endpoint).

## 2. External Interfaces

### Caller-Facing API

| Function | Arguments | Behaviour |
|---|---|---|
| soakInit | (void) | Creates the soak-monitor task if the soak-test build flag is defined. No-op when the flag is absent. |
| soakMonitorTask | (void, task entry) | Infinite loop: measures heap, logs statistics, checks DRAM threshold, reboots if below threshold, then sleeps 60 seconds. |
| soakStatsJson | (void) | Returns a JSON string with current DRAM and PSRAM heap statistics. |

### JSON Output

The soakStatsJson function produces a JSON object with the following fields:

| Field | Type | Description |
|---|---|---|
| uptime_s | unsigned 32-bit integer | Seconds since boot. |
| dram_free | unsigned 32-bit integer | Free DRAM in bytes. |
| dram_largest_block | unsigned 32-bit integer | Largest contiguous free block in DRAM. |
| psram_free | unsigned 32-bit integer | Free PSRAM in bytes (0 if PSRAM not present). |
| psram_total | unsigned 32-bit integer | Total PSRAM in bytes (0 if PSRAM not present). |

### Build Flag

| Flag | Environment | Effect |
|---|---|---|
| SOAK_TEST | 4-universe Ethernet build (esp32s3_n16r8_eth) | Enables the soak-monitor task and 60-second logging. Absent in all other environments. |

## 3. State Machine

No state machine. The task is a simple poll-and-sleep loop:

`
for (;;) { measure heap; log; reboot-if-low; sleep 60s }
`

No persistent internal state is maintained between iterations. The task does not transition between discrete states; it repeatedly measures, logs, and sleeps. The only exit path is a reboot triggered by the DRAM threshold check.

## 4. Data Flow

1. **Start**: During setup, the soakInit function is called. If the soak-test build flag is defined, a FreeRTOS task is created with a 4096-byte stack at priority 1 (lowest), unpinned to either core.

2. **Measure**: The task reads the free DRAM heap size. If the target board has PSRAM support compiled in, it also reads the free and total PSRAM heap sizes. The largest contiguous free block in DRAM is also measured.

3. **Log**: The task prints a serial log line with uptime, DRAM free, DRAM largest block, PSRAM free, and PSRAM total values.

4. **Guard**: If free DRAM is below 30 KB, the task prints a log message and triggers a hard reboot via the ESP restart function. This reboot does not perform a graceful DMX shutdown — the RMT peripheral idles the line on abandonment, producing a benign extra mark rather than a corrupted frame.

5. **Sleep**: The task delays for 60 seconds and repeats the cycle.

6. **JSON (on demand)**: The HTTP /diag/soak-stats route calls the JSON function, which builds a JSON string from live heap reads. When PSRAM is not compiled in, the PSRAM fields are emitted as 0.

## 5. Configuration Integration

None. The soak-monitor module does not read or write any runtime configuration field. The soak-test build flag is a compile-time macro, not a runtime configuration option. The PSRAM support flag is an ESP-IDF Kconfig symbol, also compile-time. The 30 KB DRAM threshold and 60 second logging interval are hardcoded constants — neither is exposed as a configuration field.

Resolution order: neutral (no config) — the module is entirely build-flag gated, not runtime-config gated.

## 6. Lifecycle

- **Init phase (core 0, setup)**: soakInit is called unconditionally during setup. When the soak-test flag is absent, the function is a no-op. When the flag is present, the monitor task is spawned.
- **Runtime**: The monitor task loops forever with a 60-second period. It runs on whichever core the FreeRTOS idle task is not using (unpinned), at the lowest priority.
- **Shutdown**: None — the only exit path is a hard reboot. There is no graceful shutdown or cleanup.

## 7. Error Handling

| Condition | Handling |
|---|---|
| DRAM below 30 KB | Logs a message and calls hard reboot. No graceful DMX shutdown — the RMT peripheral idles the line. |
| PSRAM queries on non-PSRAM build | Guarded by compile-time check; PSRAM fields default to 0 and no PSRAM heap API is called. |
| HTTP JSON read failure | None — the JSON builder reads heap live each call; if a query returns 0, it is reported as 0. |
| Task not created (flag absent) | soakInit is a no-op; no error, no log message. |

All failures in the DRAM path result in a reboot — the watchdog is designed to recover the device from a memory-leak condition by cycling power. There are no error codes or exceptions; the reboot is the only remediation.

## 8. Timing Constraints

| Item | Value |
|---|---|
| Monitor task period | 60 000 ms (60 seconds) |
| DRAM low-water threshold | 30 KB |
| Task priority | 1 (lowest) |
| Task stack | 4096 bytes |
| HTTP JSON read | On-demand, sub-millisecond |

The 60-second logging period is a best-effort diagnostic cadence, not a hard real-time deadline. The DRAM threshold check occurs once per 60-second cycle. The hard reboot on threshold violation is immediate upon detection.

## 9. Memory and Allocation Model

- The monitor task stack is 4096 bytes, allocated by the RTOS at task creation time.
- The JSON function builds its output string on the Arduino heap. The string is approximately 80 bytes — bounded and small.
- No heap_caps_malloc is used in the module itself. All heap statistics are read directly from the ESP-IDF heap introspection APIs.
- No static data structures are defined within the module — all values are read from the hardware heap at call time.

## 10. Safety Considerations

- The module runs at the lowest task priority (priority 1) and is unpinned. It never preempts the real-time DMX transmit task (priority 19) or the RDM task (priority 18) on core 1.
- The DRAM reboot threshold (30 KB) is a conservative floor above the 200 KB free-heap target used during firmware validation. When DRAM is critically low, a hard reboot is safer than continuing with potential heap corruption.
- The reboot does not gracefully shut down DMX output. The RMT peripheral, when abandoned, idles the line (a benign extra mark), so the reboot does not produce corrupted DMX frames on the wire — it simply stops transmission.
- The PSRAM guard ensures that on boards without PSRAM, no invalid heap API calls are made; the PSRAM fields are simply reported as zero.
- The 30 KB threshold only checks DRAM. A leak confined to PSRAM will not trigger a reboot until DRAM is starved by memory pressure. This is intentional — DRAM is the critical resource for real-time task stacks and ISR contexts.
- The JSON builder allocates on the Arduino heap within the web server's core-0 callback context. Repeated polling during a heap crisis could accelerate fragmentation, but the small string size (~80 bytes) mitigates this risk.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| Task Scheduling | downstream (consumer) | soakInit is called during setup alongside other sys-module initializations. |
| Web Server | downstream (consumer) | The /diag/soak-stats HTTP route calls soakStatsJson to serve heap statistics. |
| ESP-IDF Heap | internal | Provides heap_caps_get_free_size, heap_caps_get_total_size, heap_caps_get_largest_free_block. |
| Arduino HAL | internal | Provides ESP.getFreeHeap and ESP.restart. |
| Core Stats | upstream (consumed) | The uptime seconds value is sourced from the stats module's boot timestamp. |

## 12. Testing Verification

No host-native test covers the soak-monitor module — the ESP restart function, heap introspection APIs, and FreeRTOS task delay are not shimmed in the native test environment. The /diag/soak-stats JSON shape is consumed by the Playwright web end-to-end tests, but only against a live device, not a unit test. The reboot-on-low-DRAM path is a hardware-only validation: induce a DRAM leak and confirm the 60-second log entry followed by a system reboot.

## 13. Open Questions

1. Whether the soak-test build flag is set in the build configuration for the 4-universe Ethernet environment.
2. Whether a non-PSRAM build path exists for the soak-test environment; the fallback (PSRAM fields as 0) handles it, but the target board has 8 MB of PSRAM.
3. Whether the 30 KB DRAM threshold or the 60-second logging interval should be runtime-configurable for field tuning; neither exists as a configuration field.

## 14. History

The soak-test build flag gating was added so the 60-second heap logger is absent from production firmware builds. The PSRAM support guard was added to the PSRAM reporting section so that non-PSRAM build environments do not emit PSRAM statistics or make invalid heap API calls. The /diag/soak-stats HTTP route was added to expose heap statistics over the web, enabling remote monitoring of the soak test on the 4-universe build. The 30 KB DRAM watermark was chosen as a conservative floor above the 200 KB free-heap target used in the firmware evaluation workflow. The uptime value is sourced from the shared boot timestamp used by the WebSocket status frame, for consistency across monitoring interfaces.
