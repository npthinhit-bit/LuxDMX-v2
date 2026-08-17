# Syslog Specification

Domain: sys.syslog

## 1. Module Overview

The Syslog module is the gateway's remote logging client. It implements an RFC 5424 syslog sender over UDP, forwarding selected device events to a remote syslog server (for example rsyslog, Papertrail, Graylog, or a cloud log collector). Every message is also echoed to the local serial console, so disabling the remote destination never silences on-device debug output.

The module exposes three entry points: `syslogInit` (opens the UDP socket at boot), `syslogSend` (emits a pre-formatted message with an explicit severity level), and `syslogPrintf` (printf-style variadic wrapper). Senders pass the severity code per message; the module applies no independent per-severity threshold. The configured enablement flag is the single gating control that decides whether messages leave the device over the network — when disabled, output is serial-only.

Owned: the UDP syslog sender, the RFC 5424 message assembly, and the severity-to-priority computation.
Delegated to: nothing (no protocol stack; the module submits complete datagrams to the UDP/IP layer).
Consumed by: Network state (WiFi connection events), the RDM engine (RDM activity events), and any future component that emits a severity-tagged system message.

## 2. External Interfaces

### Caller-Facing API

| Function | Arguments | Behaviour |
|---|---|---|
| `syslogInit` | (void) | Opens a UDP datagram socket when the syslog enable flag is true and a server address is configured. No-op when syslog is disabled or no server is set. |
| `syslogSend` | `level` (SyslogLevel, 0-7), `msg` (string) | Echoes the message to the local console. When syslog is enabled and the send socket is open, parses the configured server address, composes an RFC 5424 datagram, and transmits it via UDP to the configured destination. |
| `syslogPrintf` | `level` (SyslogLevel, 0-7), `fmt` (format string), `...` | Formats the variadic arguments into a buffer with bounded length, then delegates to `syslogSend` with the formatted string. |

### Severity Levels

The module uses the RFC 5424 severity scale (0-7), encoded into the syslog priority value. Callers pass one of the eight severity constants.

| Code | Name | Meaning |
|---|---|---|
| 0 | Emergency | System is unusable |
| 1 | Alert | Immediate action required |
| 2 | Critical | Critical conditions |
| 3 | Error | Error conditions |
| 4 | Warning | Warning conditions |
| 5 | Notice | Normal but significant condition |
| 6 | Informational | Informational messages |
| 7 | Debug | Debug-level messages |

### Priority Computation

The syslog priority (PRI) field is computed as `facility_code * 8 + severity`. The facility code is configured per-device; the severity is passed by the caller.

| Component | Source | Range |
|---|---|---|
| Facility | configuration | 0-23 |
| Severity | caller argument | 0-7 |
| PRI | computed | 0-199 |

### RFC 5424 Wire Format

Each transmitted datagram follows the RFC 5424 frame layout:

`<PRI>VERSION TIMESTAMP HOSTNAME APP-NAME PROCID MSGID STRUCTURED-DATA MSG`

| Field | Value | Source |
|---|---|---|
| PRI | `facility * 8 + severity` | Computed |
| VERSION | `1` | Constant |
| TIMESTAMP | uptime in milliseconds (decimal) | Monotonic uptime |
| HOSTNAME | configured device hostname | Configuration |
| APP-NAME | `LuxDMX` | Constant |
| PROCID | `-` (NILVALUE) | Constant |
| MSGID | `-` (NILVALUE) | Constant |
| STRUCTURED-DATA | `-` (NILVALUE) | Constant (no structured data emitted) |
| MSG | caller-supplied message text | Caller |

The structured-data field is present in the frame but emitted as the NILVALUE placeholder, so no structured-data ID/value pairs are currently carried.

### Event Categories

The module transports severity-tagged events for remote operators:

| Category | Typical Severity | Trigger |
|---|---|---|
| WiFi connect | Notice (5) | Station obtains a DHCP address and joins the network |
| RDM activity | varies | RDM discovery, command dispatch, and response result |
| General system | varies | Boot, task, and error events from any component |

## 3. State Machine

The module has no long-lived state machine. It holds a single boolean readiness condition derived from configuration and socket acquisition:

- **Disabled**: the enable flag is false, or no server address is configured. `syslogInit` does not open a socket. `syslogSend` echoes to the console only and transmits no datagram.
- **Ready**: `syslogInit` opened a valid UDP send socket. `syslogSend` echoes to the console and transmits the datagram to the configured destination.
- **Misconfigured address**: the enable flag is true but the server address does not parse as a valid IP. `syslogSend` echoes to the console and silently drops the network transmission.

There are no persistent transitions between messages. Each `syslogSend` call re-evaluates the ready condition (enable flag, valid socket, parseable address) independently, so a runtime configuration change that toggles the enable flag is reflected on the very next call.

## 4. Data Flow

1. **Enable check**: `syslogSend` and `syslogInit` consult the enablement flag and the configured server address string. If either is absent or invalid, remote transmission is suppressed.
2. **Console echo**: Regardless of enablement, the message is written to the local serial console with the severity code, so debug output is always available on-device.
3. **Socket acquisition**: During setup, `syslogInit` creates a UDP datagram socket once (when enabled). The socket handle is retained for the lifetime of the boot.
4. **Address parsing**: For each send, the configured server address is parsed into a UDP endpoint. If parsing fails, the network transmission is skipped.
5. **Priority assembly**: The severity is combined with the configured facility code using `facility * 8 + severity` to form the PRI value.
6. **Framing**: The PRI, version, timestamp (uptime milliseconds), hostname, the constant application name `LuxDMX`, the NILVALUE placeholders for process and message ID, the NILVALUE structured-data field, and the message body are composed into a single datagram.
7. **Transmission**: The composed datagram is submitted to the UDP endpoint (configured IP and port, defaulting to 514 when no port is set).
8. **Printf path**: `syslogPrintf` formats the variadic arguments into a bounded buffer and then performs the same send path as `syslogSend`.

## 5. Configuration Integration

| Field | Key | Type | Live / Reboot | Description |
|---|---|---|---|---|
| Enable syslog | `syslog` | boolean | Live | Gates whether messages are transmitted over UDP to the remote syslog server. |
| Server address | `syslogip` | string | Reboot | IP address (or hostname) of the remote syslog server. |
| Server port | `syslogport` | integer (1-65535) | Reboot | UDP destination port. Defaults to 514 when zero/unset. |
| Facility | `syslogfac` | integer (0-23) | Reboot | Syslog facility code combined with the per-message severity. |
| Hostname | `hostname` | string | Reboot | Device hostname emitted as the HOSTNAME field in each datagram. |

Only the enable flag is live-applied during runtime; the server address, port, facility, and hostname are committed at boot. Resolution at startup follows the neutral value -> board template -> saved value order.

## 6. Lifecycle

- **Init phase (setup)**: `syslogInit` is called once during system bring-up, after configuration has been loaded. When the enable flag is true and a server address is present, it opens the UDP send socket.
- **Runtime**: `syslogSend` and `syslogPrintf` are called on-demand by any component. There is no periodic cadence; messages are emitted when system events occur.
- **Shutdown**: None. The socket is never explicitly closed; it is released by the system on reboot.

## 7. Error Handling

| Condition | Behaviour |
|---|---|
| Enable flag false | `syslogInit` opens no socket; `syslogSend` echoes to console only. |
| Server address empty | `syslogInit` returns without opening a socket; remote send suppressed. |
| Socket creation fails | The socket handle remains invalid; `syslogSend` echoes to console and skips transmission. |
| Server address does not parse | Console echo still occurs; the UDP transmission is silently skipped. |
| No remote server reachable (UDP is connectionless) | No error is detectable at send time; UDP datagrams are unacknowledged. |
| Message longer than buffer | `syslogPrintf` truncates the formatted string to the buffer bound before composing the datagram. |

All failure modes preserve the console echo so that no message is ever fully lost to the on-device developer. Network-layer failures are silent because UDP itself provides no delivery confirmation.

## 8. Timing Constraints

| Item | Value |
|---|---|
| Socket open | One-shot at boot; sub-millisecond |
| Per-message assembly | A few microseconds (integer math + snprintf) |
| UDP transmission | Non-blocking datagram submit; microseconds |
| Port default | 514 when unconfigured or zero |
| Message buffer bound | Bounded stack-local buffer; formatted messages are truncated if they exceed the bound |

There is no timeout or retry for remote delivery — UDP is fire-and-forget. A slow or absent syslog collector imposes no blocking on the calling task because the datagram is handed to the stack and the call returns immediately.

## 9. Memory and Allocation Model

- The UDP socket handle is a static module-global.
- The transmit datagram is composed in a stack-local character buffer sized to accommodate the longest expected message plus the RFC 5424 header.
- The `syslogPrintf` variant formats into a bounded stack-local buffer before delegating to `syslogSend`.
- No heap allocation occurs during message emission; no dynamic structures are retained between calls.

## 10. Safety Considerations

- `syslogSend` / `syslogPrintf` are safe to call from any task or context. The console echo and the UDP `sendto` are each effectively atomic at the stack layer, and the per-call buffers are stack-local, so concurrent callers do not observe torn state.
- Because UDP is connectionless, a misconfigured or unreachable syslog destination cannot block the calling task. The module never enters a reconnect loop or backoff stall.
- The enable flag is live-applied for the send decision, but the socket is opened only once at boot. Toggling the flag from off to on at runtime will re-enable transmission on the next message only if a socket was already opened; an off-at-boot device that is later enabled will not open a socket until the next reboot. This is a known limitation.
- The console echo is unconditional, so even a fully disabled remote syslog retains full on-device debug visibility.
- The PRI value is derived from a configured facility (0-23) and a caller-supplied severity (0-7); values outside these ranges are clamped by construction of the enum and config constraints.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| Config Engine | upstream (producer) | Supplies the enable flag, server address, port, facility, and hostname used to assemble and route datagrams. |
| System bring-up | downstream (consumer) | Calls `syslogInit` during setup, after configuration load. |
| Network state | downstream (consumer) | Emits the WiFi-connected notice event on successful station association. |
| RDM engine | downstream (consumer) | May emit RDM activity events at appropriate severities. |
| Serial console | internal | All messages are echoed here regardless of enablement. |

## 12. Testing Verification

No host-native test covers the syslog module. The UDP socket (`socket`/`sendto`), the WiFi-dependent serial path, and the IP-address parser are not shimmed in the native test environment. The only verified runtime behavior is the live observation that, with the enable flag set and a collector reachable, the WiFi-connected event appears in the remote syslog with the device hostname and a `Notice` severity. The PRI computation (`facility * 8 + severity`) and the message framing format are not unit-tested and would benefit from a host test that feeds a canned configuration and asserts the composed datagram.

## 13. Open Questions

1. Whether additional call sites beyond the WiFi-connected notice exist in the inspected source; the variadic `syslogPrintf` API is general-purpose, but only the network-state connect event was confirmed by grep.
2. Whether the structured-data position will ever carry populated SD-ID/value pairs, or remains permanently the NILVALUE placeholder.
3. Whether per-severity threshold filtering is intended: currently only the all-or-nothing enable flag exists, with no way to suppress, say, Debug messages while keeping Notice.
4. Why the server/port/facility/hostname fields are reboot-only while the enable flag is live, given that live-enabling an off-at-boot device cannot open a socket until the next restart.
5. Whether the uptime-millisecond timestamp is an intentional substitute for an RFC 3339 formatted timestamp, or an accepted deviation from strict RFC 5424.

## 14. History

The syslog module was introduced to replace ad-hoc `Serial.printf` debug output with a standards-based remote logging path. It was given its own place in the system layer so that WiFi connection events and future RDM events are uniformly routed through a single sender with a consistent RFC 5424 frame layout. The enable flag was made live-applied so operators can toggle remote logging without reflashing, while the destination address and port remained reboot-only to avoid re-resolving a live socket mid-run. The application name `LuxDMX` and the hostname field were chosen to give each datagram an unambiguous source identity for multi-gateway deployments.
