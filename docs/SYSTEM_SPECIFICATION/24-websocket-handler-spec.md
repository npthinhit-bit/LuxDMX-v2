# WebSocket Handler Subsystem â€” System Specification

## 1. Scope and Purpose

This document specifies the WebSocket command handler as a black box. It receives decoded text frames from the WebSocket server, parses JSON command payloads, and dispatches each command to the appropriate system operation. It handles channel-level control (manual DMX channel set, blackout, manual mode, output monitoring, subscription), scene operations (play, save, clear), and all RDM operations (device discovery, address set, identify, personality set, device label set). RDM operations are never executed synchronously; they are staged into a pending-action slot and executed later from the main loop, after the RDM task has released the shared RMT channel. The handler is the sole entry point for all browser-to-device control traffic.

## 2. System Context

The handler operates on the network layer on core 0. It is invoked by the WebSocket subsystem whenever a text frame arrives from any connected client. It writes DMX channel data to the seqlock-protected DMX buffer (readable by the core-1 DMX transmit task), sets runtime flags consumed by the merge engine and output initialization, triggers scene playback, and stages RDM operations for deferred execution.

**External consumers of handler-driven state changes:**
- Core-layer DMX buffer (seqlock write begin/end) â€” receives manual channel writes and blackout.
- Merge engine â€” reads the manual-mode flag to bypass network sources.
- Output initialization â€” reads the monitor-output selector to determine which output DMX the browser receives in status frames.
- Scene engine â€” receives play, save, and clear requests.
- RDM engine and task â€” receives deferred RDM operations.
- Stats module â€” receives manual-mode, RDM counts, and RDM TOD table population.

**External triggers of handler execution:**
- WebSocket subsystem (text frame arrival).
- Main loop (deferred RDM execution entry point).

## 3. Command Dispatch

The handler receives a text payload and its originating client ID. It wraps the payload in a string object and performs substring matching against known command tokens to determine the operation. The dispatch is case-sensitive and token-based: the presence of a command key anywhere in the JSON string triggers that handler. If a payload matches no known command, it is silently ignored.

The command tokens and their handler actions are:

| Command Key | Handler Action |
|---|---|
| subscribe | Update client subscription mask |
| viewout | Set the monitored output index |
| blackout | Zero all channels in the DMX buffer |
| mode | Set or clear the manual-mode flag |
| identify | Set channel identify flag and timeout |
| set | Write a single channel value |
| scene / play | Trigger scene playback |
| saveScene | Snapshot and persist a scene |
| clearScene | Erase a scene from persistent storage |
| discover | Stage RDM device discovery |
| setaddr | Stage RDM address change |
| identify | Stage RDM identify command |
| setpers | Stage RDM personality change |
| setlabel | Stage RDM device label change |

## 4. Subscribe Command

**Payload:** `{"subscribe": true, "universes": [0, 1]}`

The handler parses the universe array from the JSON payload and computes a bitmask with one bit per universe. Universes present in the array have their bits set; all others are cleared. The resulting mask is stored per-client-slot at the index computed from the client ID modulo the maximum client capacity.

The WebSocket subsystem reads this mask during each frame push to determine which universes changes should be delivered to each client. The default subscription (applied on connect by the WebSocket subsystem) covers all configured universes.

## 5. View Output Command

**Payload:** `{"viewout": 0, "out": 1}`

The handler validates that the output index is within range and that the output is enabled in the active configuration. If valid, it sets the monitor-output selector to the requested index. The output initialization module consumes this selector to determine which output DMX data is included in the WebSocket status frame, allowing the browser to inspect a specific output channel values.

If the output index is out of range or the output is disabled, the command is silently rejected with no state change.

## 6. Blackout and Manual Mode Commands

**Blackout payload:** `{"blackout": true}`

The handler zeroes all 512 channels in the seqlock-protected DMX buffer via a write-begin write-end transaction. This is a blocking buffer transaction: the seqlock write ticket is raised, the buffer is zeroed, and the ticket is lowered. Any value of the blackout field is accepted; false does not un-blackout (manual channel set or network input is required to restore channels).

**Manual mode payload:** `{"mode": "manual", "enabled": true}`

The handler sets the manual-mode flag to the value of the enabled field. The merge engine reads this flag on each frame tick; when set, network-sourced DMX data is bypassed and only manually-set channels are transmitted. The flag is transient: it is not persisted to NVS and resets to disabled on reboot. Only the string "manual" is accepted as the mode value; any other value results in no change.

## 7. Identify Command

**Payload:** `{"identify": true, "ch": 1}`

The handler validates that the channel index is within range (1 to 512). If valid, it sets the identify-channel flag to the specified channel and computes a timeout timestamp as the current time plus a fixed identify duration. The channel at the identified index is then forced to full value (255) in the DMX buffer.

The output path reads the identify-channel flag and timeout to override the specified channel value for the duration. Once the timeout elapses, the channel returns to its normal value. If the channel index is out of range, the command is silently rejected.

## 8. Set Channel Command

**Payload:** `{"set": {"ch": 1, "val": 203}}`

The handler validates that the channel index is within range (1 to 512) and that the value is within range (0 to 255). If valid, it writes the value into the seqlock-protected DMX buffer via a write-begin set-value write-end transaction. The transaction ensures tear-free access for the core-1 transmit task that reads the buffer.

If the channel index or value is out of range, the command is silently rejected with no buffer write.

## 9. Scene Commands

Three scene operations are supported, all dispatched from the same JSON payload token space:

**Play payload:** `{"scene": {"play": 0, "fade": 500}}`

The handler validates the scene index against the configured scene capacity. If valid and scene data exists for that index, it triggers scene playback with the specified fade duration in milliseconds. The scene engine takes over channel value interpolation and buffer writes for the duration of the fade.

**Save payload:** `{"saveScene": {"idx": 0, "name": "Work"}}`

The handler validates the scene index and confirms the scene engine has available slot data. If valid, it snapshots the current DMX buffer state and persists the scene (with the provided name) to non-volatile storage.

**Clear payload:** `{"clearScene": {"idx": 0}}`

The handler validates the scene index. If valid, it erases the scene data from non-volatile storage.

All three operations are silently rejected if the scene index is out of range or if the scene engine reports no data for save and play operations.

## 10. RDM Command Handling

The handler supports five RDM action types, each extracted from the JSON payload action token. Each RDM operation is staged rather than executed: the handler parses the relevant fields, stores them in the pending-action state, sets the pending-action selector, and returns immediately. Execution is deferred to the main-loop entry point.

**Action type: discover**
- Fields extracted: none.
- Stages action selector 1 (discover).

**Action type: setaddr**
- Fields extracted: UID (6-byte hex string) and target address.
- UID parsing: the 12-character hex string is split into a 2-byte manufacturer ID (first 4 hex chars) and a 4-byte device ID (last 8 hex chars).
- Stages action selector 2 (setaddr), the parsed UID, and the target address.

**Action type: identify**
- Fields extracted: UID.
- Stages action selector 3 (identify) and the parsed UID.

**Action type: setpers**
- Fields extracted: UID and personality index.
- Stages action selector 4 (setpers), the parsed UID, and the personality index.

**Action type: setlabel**
- Fields extracted: UID and label string.
- Stages action selector 5 (setlabel), the parsed UID, and the label string.

If UID parsing fails for any RDM action that requires a UID, the handler silently returns without staging the action.

## 11. Queued RDM Execution

The deferred-execution entry point runs on core 0 from the main loop. It checks the pending-action selector; if no action is pending, it returns immediately as a no-op.

When an action is pending, the selector is reset to zero before execution begins, ensuring that a new command arriving mid-execution will be staged for the next iteration rather than lost. The execution dispatches by action selector:

| Selector | Operation | Effect on State |
|---|---|---|
| 1 (discover) | Device discovery | Populates the RDM TOD table and sets the device count. Sets the RDM poll dirty flag. |
| 2 (setaddr) | Address change | Programs the target device DMX start address. Sets the RDM poll dirty flag. |
| 3 (identify) | Identify | Puts the target device into identify mode for a bounded duration. |
| 4 (setpers) | Personality | Sets the active personality index on the target device. |
| 5 (setlabel) | Device label | Writes the label string to the target device. |

Discovery operations run with an 8-second budget; the execution point is synchronous on core 0, meaning discovery blocks the main loop and WebSocket pushes for its duration. All RDM operations execute on core 0 as a synchronous fallback path, after the core-1 RDM task has released the shared RMT peripheral for the current 1 ms frame tick, preventing contention.

## 12. State Management

The handler maintains file-local static state for deferred RDM execution:

- A pending-action selector (integer: 0 = none, 1 through 5 = action type).
- A pending UID (6-byte RDM unique identifier).
- A pending target address (uint16).
- A pending personality index (uint8).
- A pending device label (33-byte character array).

The selector and pending fields are written by the command dispatch (on text-frame arrival) and read and cleared by the deferred-execution entry point (on main-loop iteration). Because both execute on core 0, no cross-core synchronization is required for the pending-action state.

The handler also reads and writes transient runtime flags: the manual-mode flag (consumed by the merge engine), the monitor-output selector (consumed by output initialization), and the identify-channel flag and timeout (consumed by the channel output path).

## 13. Concurrency Model

The handler executes entirely on core 0. Text-frame dispatch runs in the WebSocket server event callback context. Deferred RDM execution runs from the main loop. Both are core-0-only.

DMX buffer writes (channel set, blackout) use the seqlock write protocol: the write ticket is raised (odd), the data is written, and the ticket is lowered (even). This ensures the core-1 DMX transmit task never reads a torn buffer; it retries if the ticket moved or is odd.

RDM operations execute synchronously on core 0 via the deferred path. This is a fallback design: the primary RDM transport runs on core 1 at higher priority. The handler RDM path runs only after the core-1 RDM task has released the shared RMT peripheral for the current 1 ms frame tick, preventing contention.

## 14. Error Handling

| Condition | Behavior |
|---|---|
| UID parse failure | Silent return; no pending action staged, no error response sent |
| Invalid output index (viewout) | Command silently rejected; no state change |
| Invalid channel index (set, identify) | Command silently rejected; no buffer write |
| Channel value out of range (set) | Command silently rejected; no buffer write |
| Scene index out of range | Command silently rejected |
| Missing scene data (play, save) | Command silently rejected |
| No pending RDM action | Deferred execution entry point returns immediately |
| RDM discovery exceeds budget | Discovery aborts at the 8-second cap; partial results retained |
| Unrecognized command token | Payload silently ignored |
| String allocation failure per frame | Behavior is undefined; the system may drop the command silently |

### Testing Verification

No host-native tests exist for the handler directly. The command dispatch, channel set, blackout, manual-mode, and subscription paths are exercised end-to-end through browser-based E2E tests, which drive the WebSocket command channel against a live device and verify binary-frame responses. The RDM command staging and deferred execution are validated through the same E2E framework by observing the RDM TOD table and device count updates in the WebSocket status frame and the RDM JSON route.