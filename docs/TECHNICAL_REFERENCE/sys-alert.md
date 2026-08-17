# Alert — Technical Reference

Domain: sys.alert

## 1. Domain Scope

Owns the webhook alert client that POSTs a JSON payload to a configurable URL when a DMX output's network source is lost or restored. Provides two entry points: `alertSourceLost(outIdx, sourceIp)` and `alertSourceRestored(outIdx)`. A per-output boolean latch (`g_alertSent[]`) ensures only one JSON is sent per loss/restore transition — duplicate loss events are suppressed until a restore resets the latch.

The module is the **sys**-layer alert shim; it reads `cfg` fields (`webhookAlerts`, `webhookUrl`, `cfg.outputs[outIdx].universe`) and drives the ESP32 `HTTPClient` (Arduino). The **loss detection** logic itself lives in `[core-merge-engine](./core-merge-engine.md)` (`mergeOutput`, `stats().outSrcLost`), which calls into `alertSourceLost` / `alertSourceRestored` when the per-port failsafe timeout expires.

## 2. Layer Mapping

```
drv  →  cfg  →  core  →  net  →  app/sys
            ↑      ↑              ↑
      webhookUrl /   outSrcLost   alertSourceLost()
      outputs[].uni  (stats.h)    alertSourceRestored()
                     │            (sys/alert.cpp)
                     │              ↓
                     └───── mergeOutput()
                          (core-merge-engine)
                              → HTTP POST
                              (Arduino HTTPClient)
```

Reads **cfg** fields (`cfg.webhookAlerts`, `cfg.webhookUrl`, `cfg.outputs[outIdx].universe`) and the **core**-layer `stats().outSrcLost[outIdx]` latch (set by `[core-merge-engine](./core-merge-engine.md)`). Writes to the **net** boundary via HTTP POST to the caller-configured URL.

## 3. Source Files

| File | Role |
|---|---|
| `src/sys/alert.cpp` | `g_alertSent` (line 9), `alertSourceLost` (line 11), `alertSourceRestored` (line 39) |
| `src/sys/alert.h` | `alertSourceLost` (line 7), `alertSourceRestored` (line 8) declarations; includes `config_schema.h` (line 3) |
| `src/sys/tasks.cpp` | not a consumer; `updateLedFromNet` is the adjacent housekeeping task |
| `src/core/merge_engine.cpp` | sole caller: `alertSourceLost(outIdx, ip)` at line 35, `alertSourceRestored(outIdx)` at line 59 |
| `src/core/stats.h` | `outSrcLost[MAX_OUTPUTS]` declaration (line 33) — the loss flag that triggers the alert |
| `src/test_stubs.cpp` | no-op stubs for `alertSourceLost`/`alertSourceRestored` under `#ifdef UNIT_TESTING` (lines 6-11) |
| `src/cfg/config_schema.cpp` | `webhookAlerts` (line 127, BFIELD_L = CFG_LIVE), `webhookUrl` (line 128, SFIELD = CFG_REBOOT | CFG_SECRET) |
| `include/config_schema.h` | `Config.webhookAlerts/Server/Port/Facility` (lines 84-85) |

## 4. Data Structures

### `g_alertSent` (`src/sys/alert.cpp:9`)

| Field | Type | Initial | Description |
|---|---|---|---|
| `g_alertSent[MAX_OUTPUTS]` | `static bool[4]` | `{false, false, false, false}` | Per-output latch: `true` after a loss alert is sent, `false` after a restore alert is sent. Prevents duplicate POSTs for the same transition. |

### Webhook JSON payload (`src/sys/alert.cpp:21-27, 49-54`)

#### Loss payload (`alertSourceLost`, `src/sys/alert.cpp:21-27`)

```json
{"event":"dmx_loss","output":"A","universe":1,"source":"<ip>","uptime_s":<u>}
```

| Field | Type | Source |
|---|---|---|
| `event` | string literal | `"dmx_loss"` |
| `output` | `char('A' + outIdx)` | `src/sys/alert.cpp:23` |
| `universe` | `cfg.outputs[outIdx].universe` | `src/sys/alert.cpp:24` |
| `source` | `sourceIp` (optional) | `src/sys/alert.cpp:25` — only included if `sourceIp != nullptr` |
| `uptime_s` | `(millis() - 1) / 1000` | `src/sys/alert.cpp:26` |

#### Restore payload (`alertSourceRestored`, `src/sys/alert.cpp:49-54`)

```json
{"event":"dmx_restore","output":"A","universe":1,"uptime_s":<u>}
```

| Field | Type | Source |
|---|---|---|
| `event` | string literal | `"dmx_restore"` |
| `output` | `char('A' + outIdx)` | `src/sys/alert.cpp:50` |
| `universe` | `cfg.outputs[outIdx].universe` | `src/sys/alert.cpp:51` |
| `uptime_s` | `(millis() - 1) / 1000` | `src/sys/alert.cpp:52` |

Note: the restore payload **omits** `source` — there is no "restored from which IP" field.

## 5. Concurrency

**Called from both cores; no internal locking.**

- `alertSourceLost` / `alertSourceRestored` are called from `mergeOutput()` (`src/core/merge_engine.cpp:35,59`). `mergeOutput` is invoked by `mergeOutputTimed` ([core-merge-engine](./core-merge-engine.md)) which runs on **core 1** inside `dmxFrameTick` ([sys-tasks](./sys-tasks.md), `src/sys/tasks.cpp:135-136`).
- The `g_alertSent[]` latch (`src/sys/alert.cpp:9`) is a plain `bool` array written and read without `volatile` or a lock — but access is serialised by the single-writer `mergeOutput` on core 1 (one output per tick iteration).
- `WiFi.status()` check (`src/sys/alert.cpp:17,45`) and the `HTTPClient` POST are blocking calls that can stall `dmxFrameTick` for up to 3 000 ms (`http.setTimeout(3000)`, `src/sys/alert.cpp:20,48`). This is a **priority-inversion risk**: a prio-19 DMX task is blocked behind a blocking HTTP POST — see [Limitations](#17-limitations).
- The `#ifdef ESP32` guard means the host-build (native test) path is a `printf` no-op (`src/sys/alert.cpp:35,62`).

## 6. State Machine

Per-output two-state latch:

```
┌─────────┐  loss detected  ┌──────────────┐
│ QUIET   │ ──────────────→ │ ALERT_SENT   │
│ (sent=  │                  │ (sent=true,  │
│  false) │                  │  POST loss)  │
└─────────┘                  └──────┬───────┘
                                    │ restore detected
                                    ▼
                                  ┌─────────┐
                                  │ RESTORED│
                                  │ (sent=  │
                                  │  false, │
                                  │  POST   │
                                  │ restore)│
                                  └─────────┘
```

Transitions:
- `QUIET → ALERT_SENT`: `alertSourceLost` when `!g_alertSent[outIdx]` (`src/sys/alert.cpp:13-14`).
- `ALERT_SENT → RESTORED`: `alertSourceRestored` when `g_alertSent[outIdx]` (`src/sys/alert.cpp:41-42`).
- Duplicate losses while `ALERT_SENT`: suppressed (`src/sys/alert.cpp:13`).
- Duplicate restores while `QUIET`: suppressed (`src/sys/alert.cpp:41`).

## 7. Entry Points

| Entry point | Address | Caller |
|---|---|---|
| `alertSourceLost(int outIdx, const char* sourceIp)` | `src/sys/alert.cpp:11` | `mergeOutput` — source-loss branch (`src/core/merge_engine.cpp:35`) |
| `alertSourceRestored(int outIdx)` | `src/sys/alert.cpp:39` | `mergeOutput` — source-active branch (`src/core/merge_engine.cpp:59`) |

## 8. Data Flow

1. **Loss detected** — `[core-merge-engine](./core-merge-engine.md)` `mergeOutput`: if no live sender for output `outIdx` after the failsafe timeout, sets `stats().outSrcLost[outIdx] = true` (`src/core/merge_engine.cpp:32`) and calls `alertSourceLost(outIdx, ip)` (`src/core/merge_engine.cpp:35`).
2. **Dedup** — if `g_alertSent[outIdx]` is already `true`, return without POST (`src/sys/alert.cpp:13-14`).
3. **Set latch** — `g_alertSent[outIdx] = true` (`src/sys/alert.cpp:14`).
4. **Guard** — if `!cfg.webhookAlerts || cfg.webhookUrl.length() == 0`, return (`src/sys/alert.cpp:12`); if `WiFi.status() != WL_CONNECTED`, return (`src/sys/alert.cpp:17`).
5. **POST** — `HTTPClient http; http.setTimeout(3000); http.begin(cfg.webhookUrl); addHeader("Content-Type","application/json"); http.POST(payload)` (`src/sys/alert.cpp:19-31`).
6. **Log** — `Serial.printf("[ALERT] source lost on %c (HTTP %d)\n", ...)` where `%c = char('A' + outIdx)` (`src/sys/alert.cpp:32`).
7. **Cleanup** — `http.end()` (`src/sys/alert.cpp:33`).
8. **Restore** — on next `mergeOutput` with a live sender, `stats().outSrcLost[outIdx] = false` (`src/core/merge_engine.cpp:58`), then `alertSourceRestored(outIdx)` (`src/core/merge_engine.cpp:59`). Same guard/dedup (inverse), POSTs `"dmx_restore"` (`src/sys/alert.cpp:44-60`).

## 9. Protocol Layout

HTTP POST to `cfg.webhookUrl`, `Content-Type: application/json`, body is the JSON object from [Data Structures](#4-data-structures). No auth header, no signature — see [Limitations](#17-limitations).

| Field | Offset | Size |
|---|---|---|
| HTTP method | — | `POST` |
| Target | — | `cfg.webhookUrl` (string) |
| Header | — | `Content-Type: application/json` (line 30, 57) |
| Body | — | loss: `{"event":"dmx_loss","output":"A","universe":1,"source":"<ip>","uptime_s":<u>}` / restore: `{"event":"dmx_restore","output":"A","universe":1,"uptime_s":<u>}` |

## 10. Config Integration

| Field | CFG flag | Schema line | Read in (alert.cpp) |
|---|---|---|---|
| `webhookAlerts` | `CFG_LIVE` | `src/cfg/config_schema.cpp:127` (BFIELD_L) | `alertSourceLost` (line 12), `alertSourceRestored` (line 40) |
| `webhookUrl` | `CFG_REBOOT` | `src/cfg/config_schema.cpp:128` (SFIELD \| CFG_SECRET) | `alertSourceLost` (line 12, 29), `alertSourceRestored` (line 40, 56) |
| `outputs[outIdx].universe` | `CFG_LIVE` | `src/cfg/config_schema.cpp:155` (OINT_L) | `alertSourceLost` (line 24), `alertSourceRestored` (line 52) |

`webhookAlerts` applies live (`CFG_LIVE`) — toggling it at runtime takes effect immediately. `webhookUrl` requires reboot (`CFG_REBOOT`) — a URL change only applies after restart. Writes: none.

## 11. Lifecycle

- **Init**: None — `alert.cpp` has no init function; `g_alertSent[]` is zero-initialised by the C runtime (`src/sys/alert.cpp:9`).
- **Runtime**: Called on-demand from `mergeOutput` during the 1 ms `dmxFrameTick` ([sys-tasks](./sys-tasks.md)).
- **Shutdown**: None. `HTTPClient` is created/destroyed per-call (`src/sys/alert.cpp:19,33`).

## 12. Error Handling

| Condition | Behaviour | Code |
|---|---|---|
| `webhookAlerts` false or `webhookUrl` empty | early return (no POST) | `src/sys/alert.cpp:12,40` |
| `g_alertSent[outIdx]` already set (loss) | early return (dedup) | `src/sys/alert.cpp:13` |
| `g_alertSent[outIdx]` already clear (restore) | early return (dedup) | `src/sys/alert.cpp:41` |
| WiFi not connected | early return after latch set | `src/sys/alert.cpp:17,45` |
| `http.POST` returns non-2xx | return code logged via `Serial.printf` (line 32, 59); no retry | `src/sys/alert.cpp:31` |
| `sourceIp` is nullptr | `"source"` field omitted from loss JSON | `src/sys/alert.cpp:25` |
| Host build (`!ESP32`) | `printf` stub only, no HTTP | `src/sys/alert.cpp:34-36,61-63` |

The HTTP status code is logged but never acted upon — a 5xx server error does not re-attempt the POST.

## 13. Memory Allocation

- `g_alertSent[MAX_OUTPUTS]` — static `bool[4]` in `.bss` (`src/sys/alert.cpp:9`).
- `HTTPClient http` — Arduino heap-backed, created/destroyed per call (`src/sys/alert.cpp:19,33`).
- `String payload` — Arduino heap-backed, built via `+=` (`src/sys/alert.cpp:21-27,49-54`); freed at function exit.
- No `heap_caps` allocation; no static buffers.

## 14. Timing

| Item | Value | Source |
|---|---|---|
| HTTP timeout | 3000 ms | `src/sys/alert.cpp:20,48` |
| Call frequency | once per loss/restore transition (latched) | `src/sys/alert.cpp:13,41` |
| Blocking risk | up to 3 s on the prio-19 `dmxFrameTick` path if WiFi is slow | `src/core/merge_engine.cpp:35` called from `src/sys/tasks.cpp:135` |
| `outIdx` → output letter | `char('A' + outIdx)` | `src/sys/alert.cpp:23,32,50,59` |

## 15. Traceability

| Claim | Evidence |
|---|---|
| `g_alertSent[MAX_OUTPUTS]` zero-initialised | `src/sys/alert.cpp:9` |
| `alertSourceLost` guards on `webhookAlerts`/`webhookUrl` | `src/sys/alert.cpp:12` |
| Dedup: return if `g_alertSent[outIdx]` | `src/sys/alert.cpp:13` |
| Latch set true before POST | `src/sys/alert.cpp:14` |
| WiFi connect guard | `src/sys/alert.cpp:17` |
| HTTP timeout 3000 ms | `src/sys/alert.cpp:20` |
| POST payload: `dmx_loss` JSON | `src/sys/alert.cpp:21-27` |
| `source` field conditional on non-null `sourceIp` | `src/sys/alert.cpp:25` |
| Log: `[ALERT] source lost on %c (HTTP %d)` | `src/sys/alert.cpp:32` |
| `http.end()` after POST | `src/sys/alert.cpp:33` |
| `alertSourceRestored` guards + inverse dedup | `src/sys/alert.cpp:40-42` |
| POST payload: `dmx_restore` JSON | `src/sys/alert.cpp:49-54` |
| Caller: `mergeOutput` loss branch | `src/core/merge_engine.cpp:35` |
| Caller: `mergeOutput` restore branch | `src/core/merge_engine.cpp:59` |
| `outSrcLost` set in merge engine | `src/core/merge_engine.cpp:32,58` |
| `webhookAlerts` = CFG_LIVE | `src/cfg/config_schema.cpp:127` |
| `webhookUrl` = CFG_REBOOT | `src/cfg/config_schema.cpp:128` |
| Host stubs in `test_stubs.cpp` | `src/test_stubs.cpp:6-11` |

## 16. Cross-References

- `[core-merge-engine](./core-merge-engine.md)` — the sole caller: `mergeOutput` invokes `alertSourceLost` when `nc == 0` and `alertSourceRestored` when a live sender returns (`src/core/merge_engine.cpp:30-35,58-59`).
- `[core-stats](./core-stats.md)` — `stats().outSrcLost[outIdx]` is the loss flag that gates the restore transition (`src/core/stats.h:33`, read/written at `src/core/merge_engine.cpp:32,58`).
- `[sys-tasks](./sys-tasks.md)` — `mergeOutputTimed()` (called from `dmxFrameTick`, `src/sys/tasks.cpp:135`) is the execution context that triggers the alert POST, meaning the 3 s HTTP timeout can block the 1 ms DMX tick.
- `config-engine` — `cfgcore::load()` provides `webhookAlerts`/`webhookUrl` (`src/cfg/config_schema.cpp:127-128`).
- `test-infrastructure` — `src/test_stubs.cpp:6-11` provides no-op alert stubs under `UNIT_TESTING`, allowing `[core-merge-engine](./core-merge-engine.md)` host tests to link.

## 17. Limitations

- **Priority inversion**: `alertSourceLost`/`alertSourceRestored` perform a blocking `HTTPClient::POST` with a 3 000 ms timeout, but they are called from `mergeOutput` which runs on the **core-1 `dmxFrameTick`** ([sys-tasks](./sys-tasks.md), priority 19). A slow or unresponsive webhook server stalls the highest-priority DMX task for up to 3 s — this can cause missed 1 ms ticks and DMX frame jitter. There is no asynchronous dispatch (no queue, no `xTaskNotifyFromISR`, no separate low-priority HTTP task).
- **Source IP is always nullptr**: the sole caller passes `ip` which is computed as `(nc > 0 && contrib[0] < MAX_SENDERS) ? nullptr : nullptr` (`src/core/merge_engine.cpp:34`) — both ternary branches are `nullptr`, so the `"source"` field is never populated in the loss payload. The originating sender IP is not propagated to the alert.
- **No retry**: a 5xx or network failure is logged but not retried; the latch stays set, so a transient webhook outage means the restore event will still POST (latch resets on restore) but the original loss may be missed by the server.
- **No authentication**: the POST sends no auth header, signature, or secret. The `webhookUrl` is `CFG_SECRET` in the schema (`src/cfg/config_schema.cpp:128`), so it can embed a token in the URL, but the body itself is unsigned — see `docs/ota-key-management.md` for the project's signing conventions (not applied here).
- **Output-to-letter mapping**: `char('A' + outIdx)` assumes ≤26 outputs (`src/sys/alert.cpp:23,50`); with `MAX_OUTPUTS=4` this is safe, but the mapping breaks for any future expansion past output Z.

## 18. Open Questions

1. Not determinable from the inspected source code — whether a separate low-priority task or queue was intended for the webhook POST to avoid blocking `dmxFrameTick`. No such offloading path was found in `merge_engine.cpp` or `tasks.cpp`.
2. Not determinable from the inspected source code — the intended value for the `sourceIp` parameter; `merge_engine.cpp:34` passes `nullptr` unconditionally, but `src/core/sender_tracker.h` / `sender_tracker.cpp` track sender IPs — whether the caller should pass a sender IP and the nullptr is a stub, or whether source-IP omission is intentional.

## 19. Testing

- No host-native test covers `alert.cpp` directly — `HTTPClient` and `WiFi` are not shimmed.
- `src/test_stubs.cpp:6-11` provides no-op stubs for `alertSourceLost`/`alertSourceRestored` under `#ifdef UNIT_TESTING`, so `[core-merge-engine](./core-merge-engine.md)` host tests (`test/native/merge_test.cpp`) link and run without emitting webhooks — but the latch (`g_alertSent`) is not exercised in the stubbed path.
- The dedup latch, the `source` field conditional, and the `char('A' + outIdx)` mapping are pure logic that could be unit-tested with a mocked HTTP layer but are not in the inspected test files.
- Webhook delivery is validated manually by configuring `webhookUrl` to a request-echo service (e.g. webhook.site) and inducing a source loss from a live Art-Net controller.

## 20. History

- Webhook alert module split from inline `Serial.printf` in `merge_engine.cpp` during the 5-layer refactor, centralising alert transport behind `webhookAlerts`/`webhookUrl` config fields (`src/cfg/config_schema.cpp:127-128`).
- `g_alertSent[]` per-output latch added to prevent webhook storms on flickering sources — without it, a noisy link-loss timeout would POST once per `dmxFrameTick` (every 1 ms).
- `alertSourceRestored` added alongside the loss variant to support restore-event webhooks; the restore payload intentionally omits `source` (there is no single "restoring" sender).
- Host stubs (`src/test_stubs.cpp:6-11`) added so the merge-engine native tests link without `HTTPClient`; the `#ifdef ESP32` guard inside `alert.cpp:16` provides the same for non-ESP32 native compilation.
