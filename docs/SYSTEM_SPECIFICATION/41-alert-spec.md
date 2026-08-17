# Webhook Alert Specification

Domain: sys.alert

## 1. Module Overview

The Alert module is the gateway's outbound event notifier. When a DMX output loses its network source, or when the source returns, the module composes a JSON document and POSTs it to a configurable webhook URL over HTTP. Each output carries an independent latch so that only one loss notification and one restore notification are delivered per source-loss/restoration transition — duplicate events for an already-lost or already-restored output are suppressed, providing rate limiting at the transition level.

The module is the system-layer alert shim: it reads the enable flag and webhook URL from configuration, reads the per-output universe from configuration, and drives the HTTP client. The loss detection itself lives upstream in the merge engine, which invokes this module when a port's failsafe timeout expires with no live sender (loss) or when a live sender returns (restore).

Owned: the per-output alert latches, the JSON payload assembly, and the HTTP POST to the webhook URL.
Delegated to: nothing (no queue, no retry scheduler).
Consumed by: the merge engine (loss/restore transition), Web routes (the alerts summary array on diagnostic pages).

## 2. External Interfaces

### Caller-Facing API

| Function | Arguments | Behaviour |
|---|---|---|
| `alertSourceLost` | `outIdx` (output index, 0-based), `sourceIp` (string, may be null) | If alerts are enabled and a webhook URL is set, and no loss alert has already been latched for this output, latches the output and POSTs a loss JSON payload. If not enabled, returns silently. |
| `alertSourceRestored` | `outIdx` (output index, 0-based) | If alerts are enabled and a webhook URL is set, and a loss alert is currently latched for this output, clears the latch and POSTs a restore JSON payload. If no alert was latched, returns silently. |

### JSON Payloads

Both payloads are POSTed with the `Content-Type: application/json` header.

**Loss payload** (`alertSourceLost`):

| Field | Type | Included when | Description |
|---|---|---|---|
| `event` | string | always | `dmx_loss` |
| `output` | string | always | Single-letter output label (`A`, `B`, `C`, or `D`) derived from the output index. |
| `universe` | integer | always | The DMX universe number configured for this output. |
| `source` | string | only when a non-null `sourceIp` is supplied | The IP address of the lost source. |
| `uptime_s` | integer | always | Device uptime in seconds at the moment of the event. |

**Restore payload** (`alertSourceRestored`):

| Field | Type | Description |
|---|---|---|
| `event` | string | `dmx_restore` |
| `output` | string | Single-letter output label. |
| `universe` | integer | The DMX universe number configured for this output. |
| `uptime_s` | integer | Device uptime in seconds at the moment of the event. |

The restore payload intentionally omits the `source` field, since restoration is not attributable to a single sender.

### HTTP Transport

| Property | Value |
|---|---|
| Method | POST |
| Content-Type | application/json |
| Destination | configured webhook URL |
| Timeout | 3000 ms |
| Success criterion | HTTP response code received (any code); the response body is ignored |
| Retry | none (single attempt) |

## 3. State Machine

Each output maintains an independent two-state latch:

- **Quiet**: no outstanding loss alert for this output. `alertSourceRestored` is a no-op. `alertSourceLost` transitions to Alerted.
- **Alerted**: a loss alert has been delivered and not yet cleared. `alertSourceLost` is a no-op (rate-limited). `alertSourceRestored` transitions back to Quiet.

Transitions occur only on the loss/restore edge detected by the merge engine; the module itself never initiates a transition. The latch is the entire rate-limiting mechanism.

## 4. Data Flow

1. **Loss edge**: The merge engine detects that no live sender remains for an output past the failsafe timeout and calls `alertSourceLost(outIdx, sourceIp)`.
2. **Guard**: The module checks the enable flag and the webhook URL. If either is absent, it returns without side effect.
3. **Dedup**: The module checks the per-output latch. If already latched, it returns (duplicate loss suppressed).
4. **Latch**: The latch is set to Alerted so further loss edges for the same output are suppressed until a restore.
5. **Payload**: A JSON document is composed with the `dmx_loss` event, the output label, universe, optional source IP, and uptime.
6. **POST**: The JSON is posted to the configured webhook URL with a 3000 ms timeout. The HTTP response code is logged locally; the response body is discarded.
7. **Restore edge**: On the next merge pass with at least one live sender, the merge engine calls `alertSourceRestored(outIdx)`.
8. **Inverse guard**: The module checks enablement and the latch. If not latched, it returns (no spurious restore).
9. **Clear**: The latch is cleared to Quiet.
10. **Restore POST**: A `dmx_restore` JSON document (without the `source` field) is posted to the same URL.

## 5. Configuration Integration

| Field | Key | Type | Live / Reboot | Secret | Description |
|---|---|---|---|---|---|
| Enable webhook alerts | `webhook` | boolean | Live | no | Gates whether loss/restore notifications are POSTed. |
| Webhook URL | `webhookurl` | string | Reboot | yes | HTTP(S) endpoint that receives the JSON payloads. |
| Output universe | `universe` (per output) | integer | Live | no | Read to populate the `universe` field in each payload. |

The enable flag is live-applied, so operators can toggle webhook delivery at runtime. The webhook URL is reboot-only and marked secret so it is masked in configuration dumps and never echoed to the console or web UI.

## 6. Lifecycle

- **Init phase**: No explicit init call. The latches are static module-globals zero-initialized to Quiet.
- **Runtime**: Invoked on-demand by the merge engine on the core-1 DMX frame tick path when a source loss or restoration edge is detected. There is no periodic timer and no background task.
- **Shutdown**: None. Latches reset to Quiet only through the normal restore transition; a reboot clears all latches.

## 7. Error Handling

| Condition | Behaviour |
|---|---|
| Alerts disabled | Both entry points return immediately; no payload assembled, no HTTP attempted. |
| Webhook URL empty | Both entry points return immediately after the enable check. |
| WiFi not connected | The HTTP POST step is skipped; the latch still transitions so that local state remains consistent and the next restore will re-arm. |
| HTTP POST fails (timeout, connection error, non-2xx) | The latches still transition (lost→Alerted, restored→Quiet) regardless of whether the POST succeeded. The HTTP response code, or the failure, is logged locally. Webhook failure is never fatal. |
| Output index out of supported range | The latch array is bounded to the maximum configured output count; the merge engine only ever passes valid indices. |
| Duplicate loss edge | Suppressed by the latch; no second POST. |
| Spurious restore without prior loss | Suppressed by the inverse latch check; no POST. |

The module deliberately decouples latch state from delivery success: a network outage does not cause alert storms, and a failed first POST does not retry repeatedly.

## 8. Timing Constraints

| Item | Value |
|---|---|
| HTTP POST timeout | 3000 ms |
| HTTP client stack | Per-call allocation within the calling task |
| Call site | core-1 DMX frame tick path (high priority) |
| Retry / backoff | none |

The 3000 ms HTTP timeout is a hard ceiling on how long a single notification can block the calling path. Because the module is invoked synchronously from the merge path, a slow or unreachable webhook server can delay that path for up to 3 s per notification.

## 9. Memory and Allocation Model

- The per-output latch array is a static module-global boolean array, one entry per supported output, zero-initialized.
- The HTTP client instance is allocated on the calling task's stack for the duration of each POST and released at the end of the call.
- The JSON payload string is composed on the heap (Arduino `String`) within the call and discarded after the POST completes.
- No persistent heap allocations are retained between calls.

## 10. Safety Considerations

- **Priority inversion**: The entry points are called from the core-1 DMX frame tick path. The HTTP POST is synchronous, so an unresponsive webhook server can stall the high-priority transmit path for up to 3 s per notification. This can cause missed 1 ms ticks and DMX frame jitter. There is no asynchronous dispatch (no queue, no separate low-priority HTTP task) at the present time.
- **Non-fatal delivery**: Webhook delivery failure never raises an error to the merge engine and never alters DMX output behaviour. Alerts are best-effort diagnostics, not control-plane commands.
- **Rate limiting**: The per-output latch guarantees at-most-one loss and one restore notification per transition, preventing alert storms under flapping sources. There is no additional time-based rate cap beyond the latch.
- **Secret handling**: The webhook URL is marked secret in the schema and masked in dumps; it is never printed to the console or served via the web UI.
- **Delivery semantics**: HTTP POST is unacknowledged beyond the response code. A 2xx response confirms the server accepted the payload, not that it was actioned. The response body is intentionally ignored.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| Merge engine | upstream (producer) | Detects source loss/restore edges and invokes `alertSourceLost` / `alertSourceRestored`, passing the output index and (for loss) the source IP. |
| Config engine | upstream (producer) | Supplies the enable flag, webhook URL, and per-output universe. |
| Output init | upstream (indirect) | Defines the output-index namespace and the per-output universe. |
| Network state | upstream (indirect) | Provides the WiFi connectivity status that gates whether the HTTP POST is attempted. |
| Serial console | internal | Logs the local result line for each notification. |

## 12. Testing Verification

Host-native tests stub both entry points with no-op implementations (under the unit-test build), so the merge-engine host tests link and run without attempting any HTTP delivery. The latch behaviour (dedup on duplicate loss, re-arm on restore) is therefore not exercised by the stubbed path. No host test covers the JSON payload assembly or the HTTP transport. The only validation of live behaviour is operational: observe the remote webhook receiver when a source is withdrawn and restored. The payload shape (event, output label, universe, uptime) would benefit from a host test that feeds the enable flag and asserts the composed JSON, but no such test currently exists.

## 13. Open Questions

1. Whether delivery is intended to become asynchronous: currently the POST is synchronous and runs on the core-1 DMX tick path, imposing up to a 3 s blocking window per notification. Decoupling delivery into a low-priority queue/task would remove the priority-inversion risk.
2. Whether retry/backoff is intended: the module performs a single attempt with no retry and no exponential backoff. The task description calls for retry/backoff, but the inspected implementation does not provide it.
3. Whether numeric severity levels are intended in the payload: currently the `event` string (`dmx_loss` / `dmx_restore`) is the only classification, and the restore payload omits the `source` field because the restore edge does not identify a single sender.
4. Why the webhook URL is reboot-only while the enable flag is live: toggling enable at runtime takes effect immediately, but changing the destination URL requires a restart.
5. Whether the null source-IP observed at the merge-engine call site (the loss edge does not pass a usable source address) is intentional, meaning the `source` field is never populated in practice.

## 14. History

The alert module was added to give operators remote visibility into DMX source loss without requiring constant human monitoring of the gateway. The per-output latch was chosen as the rate-limiting mechanism because source loss is an edge event (loss then eventual restore), making a simple two-state latch both sufficient and robust against flapping. The enable flag was made live-applied while the URL was kept reboot-only and secret, so operators can disable noisy webhooks immediately without exposing the endpoint. The synchronous HTTP POST was implemented as the simplest correct path; the priority-inversion risk on the core-1 tick path is a known trade-off documented for future asynchronous dispatch.
