# Module Specification: Stats

## 1. Module Overview

The Stats module provides the single source of truth for runtime telemetry on the LuxDMX gateway. It maintains a global state structure that tracks frame counters, frame rates, uptime, free heap, network link quality, source status, active sender count, input source-loss flags, RDM activity counters, discovered RDM device table, inter-packet jitter, and a rate-limited circular change log. All data is held in a single statically-allocated instance with no heap allocation. The module exposes inline helper functions for live FPS and uptime, and a `maybeLog` entry point that records DMX channel changes for the currently-viewed output only.

## 2. External Interfaces

The Stats module is consumed by three external clients:

- **WebSocket frame builder** — reads frame counters, FPS, jitter, source status, active sender count, RDM counts, per-output DMX data, and TX frame deltas to assemble a binary status frame pushed ~10 times per second.
- **Web routes** — the info route consumes uptime, free heap, RSSI, and HTTP request count; the health route consumes per-output source-loss flags and receive/loss counters; the RDM route consumes device count and iterates the TOD table.
- **Merge engine** — reads the `manualMode` flag to determine whether network input should be bypassed in favor of manual override.

The module writes nothing to external systems. All state is read passively by consumers through the `stats()` accessor reference.

**Helper interfaces:**
- `outFpsLive(i)` — returns TX FPS if non-zero, otherwise output FPS, for output `i`.
- `inFpsLive(i)` — returns input FPS for output `i`.
- `uptimeSec()` — returns seconds since boot, computed from the boot timestamp and the millis counter.

## 3. State Machine

The Stats module has no discrete states. It is a passive data sink: counters and flags are updated in place by producers, and consumers read values directly. The only sequencing logic is internal to the log buffer, which operates as a ring buffer (not a state machine).

## 4. Data Flow

Data enters the Stats module from four sources:

1. **Frame router** — on each processed frame, increments global frame count and per-output input counters; updates last-frame timestamp; computes FPS; sets per-output source-loss flags; updates source status (normal/conflict/merging); calls `maybeLog` for the viewed output.
2. **DMX transmit task** — increments per-output TX frame counters via the snapshot-and-transmit path; updates last DMX transmit timestamps.
3. **RDM task** — increments RDM commands-sent and responses-received counters.
4. **WebSocket handler** — sets the `manualMode` flag on command.

Data leaves the module only through reads by the WebSocket frame builder, web routes, and merge engine. The change log flows unidirectionally: `frame_router` to `maybeLog` to circular buffer to (currently unimplemented serialization in web routes, returns empty).

## 5. Configuration Integration

The Stats module has no persisted configuration fields. It does not integrate with the config engine, NVS, serial console, or web config form. The only externally-influenced value is the `manualMode` flag, which is set transiently via WebSocket command and is not persisted across reboots. No config flag exists for `manualMode`.

The log buffer capacity, TOD table capacity, and frame length are compile-time constants, not runtime-configurable.

## 6. Lifecycle

- **Boot / Phase 1** — The boot timestamp is captured during system setup, before any tasks start.
- **Runtime** — Counters and timestamps are updated continuously as frames, RDM transactions, and WebSocket commands arrive.
- **No explicit initialization** — The global instance is zero-initialized by C++ value-initialization; no init or deinit functions exist.
- **No teardown** — The module never releases resources or resets state during runtime.

## 7. Error Handling

The Stats module performs no error handling. It is a passive accumulator. `maybeLog` silently drops entries when the rate limit has not elapsed. There are no return codes, no error flags, and no failure modes — all writes are unconditional increments or assignments. The module assumes all callers provide valid indices and values.

## 8. Timing Constraints

- **maybeLog rate limit** — at most one log entry per output per 100 ms. Entries arriving sooner than the interval are silently discarded.
- **WebSocket consumption** — the frame builder reads stats approximately every 100 ms (10 Hz). Stale reads are acceptable; no freshness guarantee is enforced.
- **Cross-core access** — fields accessed across core 0 and core 1 are qualified with `volatile` where atomicity of the read cannot be guaranteed by the consumer's existing synchronization (e.g., seqlock-protected DMX buffer snapshots). Reads of volatile fields may observe intermediate values and are consumed under the understanding that approximate telemetry is acceptable.
- **Uptime** — computed from the millis counter, so it wraps to zero after approximately 49.7 days.

## 9. Memory & Allocation Model

All state resides in a single statically-allocated global instance of approximately 400 bytes. No heap allocation occurs anywhere in the module.

**Sub-allocations within the instance:**
- Change log buffer: 32 entries x 13 bytes each (4-byte source IP, 1-byte protocol, 8-byte channel data) = 416 bytes.
- RDM TOD table: 64 entries x 6 bytes each (2-byte manufacturer ID + 4-byte device ID) = 384 bytes.
- Per-output arrays (frame counts, timestamps, FPS, TX counters, source-loss flags): 4 elements each.

Total static footprint is under 1 KB. The change log and TOD table are embedded within the instance, not separately allocated.

## 10. Safety Considerations

- **No crash guard** — Stats is a telemetry module; no output, transmission, or safety-relevant decision depends on its correctness. Loss of stats data does not affect DMX output validity.
- **Cross-core races** — `manualMode` is read by the merge engine on core 1 but is not declared volatile; it is checked in a non-real-time path, so a stale read is acceptable but should be noted.
- **RDM TOD race** — `rdmCount` and `rdmTod` are not volatile but may be written from both the WebSocket processing path and the RDM task path concurrently. This is a known race with no current mitigation.
- **Log buffer not serialized** — `maybeLog` populates the log buffer, but the web route that would serialize it currently returns an empty result. Logged data is never exposed to consumers.

## 11. Cross-Module Dependencies

- **Depends on:** The millis facility (system uptime), free heap query function, WiFi RSSI query function, active sender count function, DMX buffer snapshot/seqlock, config output's `viewOutput` (for log gating), config output's `txStyleSrc` (for WebSocket frame composition).
- **Consumed by:** WebSocket frame builder, web info/health/RDM routes, merge engine (manual mode), frame router (frame counting, source-loss, logging).
- **External types:** RDM UID type (6-byte identifier used in the TOD table).

## 12. Testing Verification

No direct host-native tests exist for the Stats module. The change log function (`maybeLog`) could be tested via the native test runner with shims for the millis counter, but no test is currently implemented. The module's behavior is validated only indirectly through WebSocket frame output and web route responses in end-to-end tests.

## 13. Open Questions

- Whether the RDM TOD table race (non-volatile access from two cores) should be resolved by adding volatile qualifiers or a mutex.
- Whether `manualMode` should be persisted across reboots as a config field, given it is currently a transient runtime toggle.
- Whether the log buffer serialization route should be implemented, since `maybeLog` populates data that is currently never exposed.

## 14. History

The Stats module was introduced as the centralized telemetry point for the 5-layer architecture rewrite, consolidating frame counting, FPS computation, RDM tracking, and source-loss detection into a single static instance. The `manualMode` flag was added as a WebSocket-driven runtime toggle rather than a persisted config field, to allow temporary override without consuming a config slot. The change log buffer was added to support per-output channel-change inspection in the WebSocket viewer, gated to the currently-viewed output to avoid flooding memory at rate.
