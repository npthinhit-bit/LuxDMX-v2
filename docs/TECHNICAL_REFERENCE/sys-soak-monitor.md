# Soak Monitor — Technical Reference

Domain: sys.soak-monitor

## 1. Domain Scope

Owns the soak-test heap watchdog: a low-priority FreeRTOS task that logs DRAM/PSRAM free heap every 60 seconds and auto-reboots the device if free DRAM drops below 30 KB. The entire module is **conditionally compiled** — only present when `LUXDMX_SOAK_TEST` is defined at build time. Also exposes a JSON snapshot (`soakStatsJson`) consumed by the `/diag/soak-stats` HTTP route.

The soak monitor is a **diagnostic-only** module — it does not drive DMX, RDM, networking, or config. It is started unconditionally from `setup()` (`src/main.cpp:112`), but `soakInit()` is a no-op when `LUXDMX_SOAK_TEST` is undefined.

## 2. Layer Mapping

```
drv  →  cfg  →  core  →  net  →  app/sys
                              │
                         soakInit()
                         soakMonitorTask()
                         soakStatsJson()
                              ↓
                         ESP heap_caps
                         (ESP-IDF)
```

The module is entirely within the **sys** layer. It calls ESP-IDF heap primitives (`heap_caps_get_free_size`, `heap_caps_get_total_size`, `heap_caps_get_largest_free_block`) and Arduino (`ESP.getFreeHeap`) — both are HAL, not a project layer. It does not read `Config` fields.

## 3. Source Files

| File | Role |
|---|---|
| `src/sys/soak_monitor.cpp` | `soakInit` (line 9), `soakMonitorTask` (line 15), `soakStatsJson` (line 45) |
| `src/sys/soak_monitor.h` | `soakInit` (line 8), `soakMonitorTask` (line 9), `soakStatsJson` (line 11) — declarations |
| `src/main.cpp` | `soakInit()` call site (line 112) |
| `src/net/web_server.cpp` | `/diag/soak-stats` route serving `soakStatsJson()` (line 48) |
| `src/sys/sys_platform.h` | `uptimeSec()` inline used by `soakStatsJson` (`src/sys/tasks.cpp:17` of header) |
| `src/core/stats.h` | `stats().startMs` used by `uptimeSec()` (`src/core/stats.h:67`) |

## 4. Data Structures

No project-defined structs. The module uses only ESP-IDF / Arduino primitives:

| Type | Source | Used in |
|---|---|---|
| `size_t` | `<Arduino.h>` / `<esp_heap_caps.h>` | `psramFree`, `psramTotal`, `dramFreeBlock` |
| `uint32_t` | `<Arduino.h>` | `now`, `dramFree` |
| `String` | `<Arduino.h>` | `soakStatsJson` return value |

### Conditional compilation

- `LUXDMX_SOAK_TEST` — when defined, `soakInit()` creates the monitor task (`src/sys/soak_monitor.cpp:10-12`). Undefined in normal builds — `soakInit` body is empty (`src/sys/soak_monitor.cpp:10`).
- `CONFIG_SPIRAM_SUPPORT` — when defined, PSRAM stats are reported (`src/sys/soak_monitor.cpp:22-24`); otherwise PSRAM fields default to 0 (`src/sys/soak_monitor.cpp:50-55`).

## 5. Concurrency

**Single background task, priority 1, unpinned.**

- `soakMonitorTask` is created by `soakInit()` with a 4096-byte stack, priority 1, no core pinning (`src/sys/soak_monitor.cpp:11`) — runs on whichever core the FreeRTOS idle task is not using.
- Period: 60 000 ms (`vTaskDelay(pdMS_TO_TICKS(60000))`, `src/sys/soak_monitor.cpp:41`).
- `soakStatsJson()` is called from the HTTP handler context (core 0, AsyncWebServer callback) on demand (`src/net/web_server.cpp:48`) — it reads `ESP.getFreeHeap()` and `heap_caps_get_*` directly, so no lock is needed against the 60-second logger. The reads are atomic-sized ESP-IDF calls.
- `g_alertSent`-style static state: none — `soakStatsJson` is stateless (reads heap live each call).

## 6. State Machine

No state machine. The task is a simple poll-and-sleep loop:

```
for (;;) { measure heap; log; reboot-if-low; sleep 60s }
```

No persistent internal state is maintained between iterations.

## 7. Entry Points

| Entry point | Address | Caller |
|---|---|---|
| `soakInit()` | `src/sys/soak_monitor.cpp:9` | `setup()` (`src/main.cpp:112`) |
| `soakMonitorTask` | `src/sys/soak_monitor.cpp:15` | FreeRTOS scheduler (created via `xTaskCreate` at `src/sys/soak_monitor.cpp:11`, only when `LUXDMX_SOAK_TEST`) |
| `soakStatsJson()` | `src/sys/soak_monitor.cpp:45` | `/diag/soak-stats` HTTP route (`src/net/web_server.cpp:47-49`) |

## 8. Data Flow

1. **Start** — `setup()` calls `soakInit()` (`src/main.cpp:112`); if `LUXDMX_SOAK_TEST` is defined, `xTaskCreate(soakMonitorTask, ...)` spawns the monitor (`src/sys/soak_monitor.cpp:11`).
2. **Measure** — `soakMonitorTask` reads `ESP.getFreeHeap()` for DRAM (`src/sys/soak_monitor.cpp:18`), and if `CONFIG_SPIRAM_SUPPORT` is defined, `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` and `heap_caps_get_total_size(MALLOC_CAP_SPIRAM)` for PSRAM (`src/sys/soak_monitor.cpp:23-24`). DRAM largest free block via `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)` (`src/sys/soak_monitor.cpp:27`).
3. **Log** — `Serial.printf("[SOAK] uptime=%lu dram_free=%u dram_block=%u psram_free=%u/%u\n", ...)` (`src/sys/soak_monitor.cpp:29-34`).
4. **Guard** — if `dramFree < 30 * 1024` (30 KB), print `"[SOAK] DRAM < 30KB, rebooting"` and `ESP.restart()` (`src/sys/soak_monitor.cpp:36-39`).
5. **Sleep** — `vTaskDelay(pdMS_TO_TICKS(60000))` (60 s) (`src/sys/soak_monitor.cpp:41`).
6. **JSON (on demand)** — HTTP `/diag/soak-stats` calls `soakStatsJson()` (`src/net/web_server.cpp:48`) which builds a JSON string with `uptime_s`, `dram_free`, `dram_largest_block`, `psram_free`, `psram_total` (`src/sys/soak_monitor.cpp:45-58`). When `CONFIG_SPIRAM_SUPPORT` is undefined, `psram_free`/`psram_total` are emitted as `0` (`src/sys/soak_monitor.cpp:54-55`).

## 9. Protocol Layout

The JSON emitted by `soakStatsJson` (`src/sys/soak_monitor.cpp:45-58`):

```
soakStatsJson → {"uptime_s":<u32>,"dram_free":<u32>,"dram_largest_block":<u32>,"psram_free":<u32>,"psram_total":<u32>}
```

| Field | Type | Source |
|---|---|---|
| `uptime_s` | integer | `uptimeSec()` → `src/core/stats.h:67` |
| `dram_free` | integer | `ESP.getFreeHeap()` (`src/sys/soak_monitor.cpp:48`) |
| `dram_largest_block` | integer | `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT|MALLOC_CAP_INTERNAL)` (`src/sys/soak_monitor.cpp:49`) |
| `psram_free` | integer | `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` or `0` (`src/sys/soak_monitor.cpp:50-51`) |
| `psram_total` | integer | `heap_caps_get_total_size(MALLOC_CAP_SPIRAM)` or `0` (`src/sys/soak_monitor.cpp:52`) |

Served over HTTP `application/json` at `/diag/soak-stats` (`src/net/web_server.cpp:48`).

## 10. Config Integration

None. `soak_monitor.cpp` does not read or write any `Config` field. The `LUXDMX_SOAK_TEST` flag is a **build-time** macro (set via `platformio.ini` build flag for the `esp32s3_n16r8_eth` environment per `CLAUDE.md`), not a runtime config field. The `CONFIG_SPIRAM_SUPPORT` flag is an ESP-IDF Kconfig symbol, also build-time.

## 11. Lifecycle

- **Init (core 0, `setup()`):** `soakInit()` (`src/main.cpp:112`) — no-op unless `LUXDMX_SOAK_TEST` (`src/sys/soak_monitor.cpp:10`).
- **Runtime:** `soakMonitorTask` loops forever (60 s cadence) once the scheduler starts after `createTasks()` (`src/main.cpp:130`).
- **Shutdown:** None — `ESP.restart()` is the only exit path.

## 12. Error Handling

- DRAM check: if `dramFree < 30 KB`, the task logs and calls `ESP.restart()` — a hard reboot with no graceful DMX shutdown (`src/sys/soak_monitor.cpp:36-39`).
- PSRAM queries are guarded by `#ifdef CONFIG_SPIRAM_SUPPORT` (`src/sys/soak_monitor.cpp:22`) — on non-PSRAM builds, `psramFree`/`psramTotal` remain 0 and no `heap_caps_get_*` call is made.
- HTTP JSON: `soakStatsJson` constructs the string from live heap reads; no error path — if a heap query returns 0, it is reported as 0.

## 13. Memory Allocation

- `soakMonitorTask` stack: 4096 bytes (`src/sys/soak_monitor.cpp:11`).
- `soakStatsJson` builds the JSON `String` on the **Arduino heap** (`src/sys/soak_monitor.cpp:46`) — bounded; the string is ~80 bytes. No `heap_caps_malloc` in the module.
- No static buffers; all data is read directly from ESP-IDF heap introspection APIs.

## 14. Timing

| Item | Value | Source |
|---|---|---|
| Monitor task period | 60 000 ms | `src/sys/soak_monitor.cpp:41` |
| DRAM low-water threshold | 30 KB | `src/sys/soak_monitor.cpp:36` |
| Task priority | 1 (lowest) | `src/sys/soak_monitor.cpp:11` |
| Task stack | 4096 bytes | `src/sys/soak_monitor.cpp:11` |
| HTTP JSON read | on-demand, sub-millisecond | `src/sys/soak_monitor.cpp:45-58` |

The 60-second period is a best-effort diagnostic cadence, not a hard real-time deadline.

## 15. Traceability

| Claim | Evidence |
|---|---|
| `soakInit` creates task only under `LUXDMX_SOAK_TEST` | `src/sys/soak_monitor.cpp:10-12` |
| Task created: name "soak", 4096 stack, prio 1, unpinned | `src/sys/soak_monitor.cpp:11` |
| DRAM free read via `ESP.getFreeHeap` (core 0) | `src/sys/soak_monitor.cpp:18` |
| PSRAM reads guarded by `CONFIG_SPIRAM_SUPPORT` | `src/sys/soak_monitor.cpp:22-24` |
| DRAM largest free block: `MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL` | `src/sys/soak_monitor.cpp:27` |
| Log format: `[SOAK] uptime=.. dram_free=.. dram_block=.. psram_free=..` | `src/sys/soak_monitor.cpp:29-34` |
| Reboot threshold: `dramFree < 30*1024` | `src/sys/soak_monitor.cpp:36` |
| Reboot message + `ESP.restart()` | `src/sys/soak_monitor.cpp:37-39` |
| Sleep 60 s | `src/sys/soak_monitor.cpp:41` |
| `soakStatsJson` JSON field order | `src/sys/soak_monitor.cpp:46-57` |
| `uptime_s` from `uptimeSec()` → `stats().startMs` | `src/sys/soak_monitor.cpp:47`, `src/core/stats.h:67` |
| Non-PSRAM fallback: psram fields = 0 | `src/sys/soak_monitor.cpp:54-55` |
| `soakInit()` called from `setup()` | `src/main.cpp:112` |
| `/diag/soak-stats` route serves `soakStatsJson()` | `src/net/web_server.cpp:48` |

## 16. Cross-References

- `[sys-tasks](./sys-tasks.md)` — `versionCheckTask` (prio 1, 60 s) is the only other prio-1 task; both compete for the same idle-core slot (`src/sys/tasks.cpp:79-81`).
- `[core-stats](./core-stats.md)` — `stats().startMs` underpins `uptimeSec()` in the JSON output (`src/core/stats.h:67`); `soakStatsJson` reads the same DRAM heap `stats()` does not — independent reads.
- `config-engine` — no config dependency; the module is build-flag gated, not runtime config gated.
- `sys-tasks` — `soakInit()` is called alongside `sysLoggerInit`/`initOTA`/`rdmTaskInit` in `setup()` (`src/main.cpp:110-115`).

## 17. Limitations

- The DRAM reboot threshold (30 KB, `src/sys/soak_monitor.cpp:36`) is hardcoded — not configurable via `Config` or `platformio.ini`. A board with a legitimately tight DRAM footprint may reboot spuriously.
- The SOAK log and the `/diag/soak-stats` JSON both read heap live — there is a ~60 s window where the logged value and the HTTP-queried value can differ; no sampling consistency guarantee.
- `ESP.restart()` is called with no DMX graceful-shutdown path — an in-progress RMT frame transmission is aborted mid-BREAK. The RMT driver idles the line on abandonment (`[drv-dmx-rmt-tx](./drv-dmx-rmt-tx.md)` notes the benign idle-on-time-slot behavior).
- The 30 KB DRAM threshold only checks DRAM; a PSRAM leak that does not yet cross into DRAM will not trigger a reboot until DRAM is starved.
- `soakStatsJson` returns a `String` allocated on the Arduino heap (`src/sys/soak_monitor.cpp:46`) — called from the AsyncWebServer core-0 context; repeated polling during a heap crisis could accelerate fragmentation.

## 18. Open Questions

1. Not determinable from the inspected source code — whether `LUXDMX_SOAK_TEST` is set in `platformio.ini` for the `esp32s3_n16r8_eth` environment (CLAUDE.md mentions it, but the actual `-DLUXDMX_SOAK_TEST` build flag location was not verified in `platformio.ini`).
2. Not determinable from the inspected source code — whether there is a non-SPIRAM build path where `CONFIG_SPIRAM_SUPPORT` is undefined for the `esp32s3_n16r8_eth` env; the fallback (`psram_free:0`, `psram_total:0`) handles it but the real board has 8 MB PSRAM.
3. Not determinable from the inspected source code — whether the 30 KB threshold or 60 s period should be `Config`-driven for field tuning; neither exists in the inspected `config_schema.cpp` OUTPUT_FIELDS or CONFIG_FIELDS.

## 19. Testing

- No host-native test covers `soak_monitor.cpp` — `ESP.restart()`, `ESP.getFreeHeap()`, `heap_caps_get_*`, and `vTaskDelay` are not shimmed in `test/native/`.
- The `/diag/soak-stats` JSON shape is consumed by the Playwright Web E2E tests (`docs/`), but only against a live device — not a unit test.
- The reboot-on-low-DRAM path is a hardware-only validation: induce a DRAM leak and confirm the 60-second log + restart.
- `config_test.cpp` does not reference `soakStatsJson`, `LUXDMX_SOAK_TEST`, or `dramFree` (`test/native/config_test.cpp`).

## 20. History

- `LUXDMX_SOAK_TEST` build-flag gating added so the 60-second logger is absent from production firmware (`src/sys/soak_monitor.cpp:10`).
- `CONFIG_SPIRAM_SUPPORT` guard added to the PSRAM reporting section (`src/sys/soak_monitor.cpp:22`) so non-PSRAM builds (e.g. `esp32dev`) do not emit PSRAM stats.
- `/diag/soak-stats` route added to `src/net/web_server.cpp:47-49` to expose `soakStatsJson()` over HTTP, enabling remote monitoring of the soak test on the `esp32s3_n16r8_eth` 4-universe build.
- The 30 KB DRAM watermark (`src/sys/soak_monitor.cpp:36`) chosen as a conservative floor above the ~200 KB free-heap target in the firmware evaluation workflow (`CLAUDE.md` "Firmware Evaluation Workflow").
- `uptime_s` sourced from `stats().startMs` (`src/core/stats.h:67`) rather than a dedicated soak-module timer, for consistency with the WebSocket uptime field (`src/net/ws_frame.cpp`).
