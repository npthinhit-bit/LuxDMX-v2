# Syslog — Technical Reference

Domain: sys.syslog

## 1. Domain Scope

Owns the UDP syslog client (RFC 5424) that forwards log messages to a remote syslog server when `cfg.syslogEnabled` is true. Provides three entry points: `syslogInit()` (opens the UDP socket), `syslogSend()` (sends a pre-formatted message), and `syslogPrintf()` (printf-style variadic wrapper). Every message is also echoed to local `Serial` for debugging. The facility code and server/port come from `Config`; no message categorisation is applied — callers pass the level.

Consumers:
- `src/main.cpp:110` — `syslogInit()` called during `setup()`.
- `src/net/net_state.cpp:182` — `syslogPrintf(SYSLOG_NOTICE, ...)` on WiFi connect.

## 2. Layer Mapping

```
drv  →  cfg  →  core  →  net  →  app/sys
         ↑                            ↑
    cfg.syslogEnabled / .syslogServer   syslogInit/Send/Printf
    /syslogPort / .syslogFacility      (sys/syslog.cpp)
         |                              ↓
         |                          ESP32 WiFi +
         |                          lwip UDP socket
         |                          (raw socket(), sendto)
         └────────────────────────── cfg read
```

Reads config from the **cfg** layer (`cfg.syslogEnabled`, `cfg.syslogServer`, `cfg.syslogPort`, `cfg.syslogFacility`) and drives the **drv**-boundary ESP32 Arduino/WiFi HAL (`WiFiUdp`, raw `socket`/`sendto` via lwip). All logic lives in the **sys** layer (`src/sys/syslog.cpp`).

## 3. Source Files

| File | Role |
|---|---|
| `src/sys/syslog.cpp` | `SyslogLevel` is in the header; `g_syslogSock` (line 14), `syslogInit` (line 16), `syslogSend` (line 23), `syslogPrintf` (line 51) |
| `src/sys/syslog.h` | `SyslogLevel` enum (lines 6-15), `syslogInit`/`syslogSend`/`syslogPrintf` declarations (lines 17-19) |
| `src/sys/sys_platform.h` | not directly — `PREF_NS` (`"dmxgw"`) is the shared NVS namespace, but syslog does not use NVS. |
| `src/main.cpp` | `syslogInit()` call (`src/main.cpp:110`) |
| `src/net/net_state.cpp` | sole consumer: `syslogPrintf(SYSLOG_NOTICE, ...)` (`src/net/net_state.cpp:182`) |
| `src/cfg/config_schema.cpp` | `syslogEnabled` (`SYSLOG` field, line 123, CFG_LIVE), `syslogServer` (line 124, CFG_REBOOT), `syslogPort` (line 125, CFG_REBOOT), `syslogFacility` (line 126, CFG_REBOOT) |
| `include/config_schema.h` | `Config.syslogEnabled/Server/Port/Facility` fields (lines 80-83) |

## 4. Data Structures

### `SyslogLevel` (`src/sys/syslog.h:6-15`)

RFC 5424 severity values:

| Constant | Value | RFC 5424 name |
|---|---|---|
| `SYSLOG_EMERG` | 0 | Emergency |
| `SYSLOG_ALERT` | 1 | Alert |
| `SYSLOG_CRIT` | 2 | Critical |
| `SYSLOG_ERR` | 3 | Error |
| `SYSLOG_WARN` | 4 | Warning |
| `SYSLOG_NOTICE` | 5 | Notice |
| `SYSLOG_INFO` | 6 | Informational |
| `SYSLOG_DEBUG` | 7 | Debug |

### `g_syslogSock` (`src/sys/syslog.cpp:14`)

| Field | Type | Initial | Description |
|---|---|---|---|
| `g_syslogSock` | `static int` | `-1` | lwip UDP socket fd; `-1` means "not initialised / disabled". |

### Syslog message format (`src/sys/syslog.cpp:40-41`)

```
<PRI>1 <TIMESTAMP_MS> <HOSTNAME> LuxDMX - - <MSG>
```

| Token | Value | Code |
|---|---|---|
| `<PRI>` | `(facility * 8) + level` | `src/sys/syslog.cpp:38` |
| `1` | RFC 5424 version (literal) | `src/sys/syslog.cpp:40` |
| `<TIMESTAMP_MS>` | `millis()` | `src/sys/syslog.cpp:40` |
| `<HOSTNAME>` | `cfg.hostname` | `src/sys/syslog.cpp:41` |
| `LuxDMX` | app-name (literal) | `src/sys/syslog.cpp:41` |
| `-` | procid (unknown) | `src/sys/syslog.cpp:41` |
| `-` | msgid (none) | `src/sys/syslog.cpp:41` |
| `<MSG>` | caller-provided text | `src/sys/syslog.cpp:41` |

## 5. Concurrency

**Single-threaded initialisation, best-effort multi-call.**

- `syslogInit()` runs once during `setup()` on core 0 (`src/main.cpp:110`). It creates a single UDP socket — no core pinning, no task.
- `syslogSend` / `syslogPrintf` are callable from any task/context. The current consumer is `net_state.cpp` on core 0 (`src/net/net_state.cpp:182`, the WiFi-connected event is synchronous in `setup`/event handler context). If a future caller posts from core 1, the `sendto` call on the shared `g_syslogSock` would be unsynchronised — but `sendto` on a UDP socket is effectively atomic at the lwip layer (single datagram). No lock is used.
- The `line[576]` buffer (`src/sys/syslog.cpp:39`) and `buf[512]` in `syslogPrintf` (`src/sys/syslog/syslog.cpp:52`) are stack-local — reentrant if called from multiple tasks, at the cost of parallel stack usage.

## 6. State Machine

No state machine. The module is a stateless send-only client aside from the binary `g_syslogSock < 0` (disabled / failed) vs `g_syslogSock >= 0` (open) liveness check.

- **Disabled**: `g_syslogSock == -1` — `syslogSend` echoes to Serial only, skips the socket (`src/sys/syslog.cpp:33`).
- **Enabled**: `g_syslogSock >= 0` — `syslogSend` echoes to Serial AND sends the UDP datagram (`src/sys/syslog.cpp:33-47`).

## 7. Entry Points

| Entry point | Address | Caller |
|---|---|---|
| `syslogInit()` | `src/sys/syslog.cpp:16` | `setup()` (`src/main.cpp:110`) |
| `syslogSend(SyslogLevel, const char* msg)` | `src/sys/syslog.cpp:23` | `syslogPrintf` (`src/sys/syslog.cpp:57`) and any future direct caller |
| `syslogPrintf(SyslogLevel, const char* fmt, ...)` | `src/sys/syslog.cpp:51` | `net_state.cpp:182` |

## 8. Data Flow

1. **Init** — `syslogInit` checks `cfg.syslogEnabled` and `cfg.syslogServer.length() == 0` (`src/sys/syslog.cpp:17`); if either fails, return without opening a socket. Otherwise `socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)` → store in `g_syslogSock` (`src/sys/syslog.cpp:19-20`).
2. **Send** — `syslogSend` always echoes to `Serial.printf` (`src/sys/syslog.cpp:25-29`). Then re-checks enabledness (`src/sys/syslog.cpp:31`); if disabled or socket invalid (`g_syslogSock < 0`, `src/sys/syslog.cpp:33`), return.
3. **IP parse** — `ip.fromString(cfg.syslogServer)` (`src/sys/syslog.cpp:35`); if the configured server string is not a valid IP, return silently (`src/sys/syslog.cpp:35`).
4. **Format** — `snprintf` into `line[576]`: `<PRI>1 <millis> <hostname> LuxDMX - - <msg>` with `pri = facility*8 + level` (`src/sys/syslog.cpp:38-41`).
5. **Destination** — `sockaddr_in dst` with `sin_port = htons(syslogPort > 0 ? syslogPort : 514)` and `sin_addr.s_addr = ip` (`src/sys/syslog.cpp:45-46`). Port 514 is the RFC default when `cfg.syslogPort == 0`.
6. **Transmit** — `sendto(g_syslogSock, line, strlen(line), 0, &dst, sizeof(dst))` (`src/sys/syslog.cpp:47`). Return value ignored.
7. **Printf wrapper** — `syslogPrintf` formats into `buf[512]` via `vsnprintf` (`src/sys/syslog.cpp:52-55`), then delegates to `syslogSend` (`src/sys/syslog/syslog.cpp:57`).

## 9. Protocol Layout

Wire format per RFC 5424, partially implemented:

```
<PRI>1 <millis> <hostname> LuxDMX - - <message>
```

| Field | Code |
|---|---|
| `<PRI>` | `(cfg.syslogFacility * 8) + level` — standard PRI = facility×8+severity (`src/sys/syslog.cpp:38`) |
| `1` | RFC 5424 version literal |
| timestamp | `millis()` (ms since boot, NOT an RFC 3339 date — a deviation from the standard, acceptable for LAN diagnostics) |
| hostname | `cfg.hostname` |
| app-name | `LuxDMX` |
| procid / msgid | `-` (omitted) |
| message | caller text |

The PRI byte is correct per RFC 5424 (facility 0–23 × 8 + severity 0–7 = 0–191). The timestamp deviates (epoch-ms vs RFC 3339) — no timezone or NTP sync is applied.

## 10. Config Integration

| Field | CFG flag | Schema line | Read in (syslog.cpp) |
|---|---|---|---|
| `syslogEnabled` | `CFG_LIVE` | `src/cfg/config_schema.cpp:123` (BFIELD_L) | `syslogInit` (line 17), `syslogSend` (line 31) |
| `syslogServer` | `CFG_REBOOT` | `src/cfg/config_schema.cpp:124` (SFIELD) | `syslogInit` (line 17), `syslogSend` (line 31) |
| `syslogPort` | `CFG_REBOOT` | `src/cfg/config_schema.cpp:125` (IFIELD) | `syslogSend` (line 45) — defaults to 514 |
| `syslogFacility` | `CFG_REBOOT` | `src/cfg/config_schema.cpp:126` (IFIELD) | `syslogSend` (line 38) |
| `hostname` | `CFG_KEEPNE` | `src/cfg/config_schema.cpp:47` (SFIELD) | `syslogSend` (line 41) |

`syslogEnabled` applies live (`CFG_LIVE`) — toggling it at runtime enables/disables the socket-less echo and re-enables sending without re-init (note: `syslogInit` is only called at boot, so enabling `syslogEnabled` at runtime does **not** open the socket until reboot — see [Limitations](#17-limitations)). `syslogServer`/`Port`/`Facility` require reboot (`CFG_REBOOT`). Writes: none.

## 11. Lifecycle

- **Init (core 0, `setup()`):** `syslogInit()` (`src/main.cpp:110`) — opens the UDP socket (or no-ops if disabled).
- **Runtime:** `syslogSend`/`syslogPrintf` called on-demand from any context; currently only from `net_state.cpp:182`.
- **Shutdown:** None — the socket fd is never closed. On reboot it is reclaimed by the OS.

## 12. Error Handling

| Condition | Behaviour | Code |
|---|---|---|
| `syslogEnabled` false | `syslogInit` returns without opening socket; `syslogSend` echoes Serial only | `src/sys/syslog.cpp:17,31` |
| `syslogServer` empty | same as above | `src/sys/syslog.cpp:17,31` |
| `socket()` returns < 0 | `g_syslogSock` stays -1; `syslogSend` skips the `sendto` | `src/sys/syslog.cpp:19-20,33` |
| `ip.fromString` fails | `syslogSend` returns after the Serial echo | `src/sys/syslog.cpp:35` |
| `sendto` return value | ignored — no retry, no error log | `src/sys/syslog.cpp:47` |
| Host build (non-ESP32) | `syslogSend` compiles to `printf("[SYSLOG %d] %s\n", ...)` only; socket code `#ifdef ESP32` excluded | `src/sys/syslog/syslog.cpp:26-29` |

The `SyslogLevel` is cast to `int` for the Serial echo (`src/sys/syslog.cpp:26`). No `ESP_LOGE` is used — failures are silent.

## 13. Memory Allocation

- `g_syslogSock` — `static int` in `.bss` (`src/sys/syslog.cpp:14`).
- `line[576]` — stack-local `char` array in `syslogSend` (`src/sys/syslog.cpp:39`); RFC 5424 max message is 2048 bytes, but the 576-byte buffer is sufficient for the short messages used here.
- `buf[512]` — stack-local `char` array in `syslogPrintf` (`src/sys/syslog.syslog.cpp:52`).
- `g_syslogSock` — a single fd, no heap allocation in the module itself. The lwip socket is managed by the ESP-IDF/lwip stack.

## 14. Timing

No hard deadline. `syslogSend` performs:
- A `snprintf` into 576 bytes (microseconds).
- One `sendto` UDP send (sub-millisecond on a local LAN, unless the network is congested — `sendto` on a UDP socket is non-blocking here since the socket was created with default flags).

The 8 s HTTP-style timeouts in other modules are irrelevant; syslog has no timeout. `syslogPrintf`'s `syslogSend` call path has no measurable latency budget.

| Item | Value | Source |
|---|---|---|
| `syslogInit` | one-shot at boot | `src/main.cpp:110` |
| Default UDP port | 514 | `src/sys/syslog.cpp:45` |
| Max msg buffer | 576 bytes | `src/sys/syslog.cpp:39` |
| Max printf buffer | 512 bytes | `src/sys/syslog.syslog.cpp:52` |

## 15. Traceability

| Claim | Evidence |
|---|---|
| `g_syslogSock` initialised to -1 | `src/sys/syslog.cpp:14` |
| `syslogInit` guards on enabled + non-empty server | `src/sys/syslog.cpp:17` |
| `socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)` | `src/sys/syslog.cpp:19` |
| `syslogSend` echoes to Serial first | `src/sys/syslog.cpp:25-29` |
| Re-check of enabled + socket validity before send | `src/sys/syslog.cpp:31-33` |
| IP parse via `ip.fromString` | `src/sys/syslog.cpp:35` |
| PRI = `facility*8 + level` | `src/sys/syslog.cpp:38` |
| Format: `<PRI>1 <millis> <hostname> LuxDMX - - <msg>` | `src/sys/syslog.cpp:40-41` |
| Port defaults to 514 if `syslogPort == 0` | `src/sys/syslog.cpp:45` |
| `sendto` with `sockaddr_in dst` | `src/sys/syslog.cpp:43-47` |
| `syslogPrintf` uses `vsnprintf` into `buf[512]` | `src/sys/syslog/syslog.cpp:52-55` |
| `SyslogLevel` enum matches RFC 5424 severity | `src/sys/syslog.h:6-15` |
| `syslogEnabled` is CFG_LIVE | `src/cfg/config_schema.cpp:123` |
| `syslogServer`/`Port`/`Facility` are CFG_REBOOT | `src/cfg/config_schema.cpp:124-126` |
| `syslogInit` called from `setup()` | `src/main.cpp:110` |
| `syslogPrintf` called from `net_state.cpp` | `src/net/net_state.cpp:182` |

## 16. Cross-References

- `config-engine` — provides `cfg.syslogEnabled/Server/Port/Facility` and `cfg.hostname` (`src/cfg/config_schema.cpp:47,123-126`).
- `[sys-tasks](./sys-tasks.md)` — `syslogInit` is called in `setup()` adjacent to `soakInit` (`src/main.cpp:110-112`); syslog itself runs without a dedicated task.
- `[net-net-state]` (not yet in this set) — `net_state.cpp:182` is the sole `syslogPrintf` consumer.
- RFC 5424 reference — see `docs/ota-key-management.md` for project-wide external-spec conventions (syslog follows the same RFC-citation style).

## 17. Limitations

- `syslogInit` is called **only at boot** (`src/main.cpp:110`). If `syslogEnabled` is set live via the web UI (it is `CFG_LIVE`, `src/cfg/config_schema.cpp:123`), the socket is **not** re-opened until the next reboot — the `syslogSend` live-check on `g_syslogSock < 0` will silently drop all messages (`src/sys/syslog.cpp:33`).
- The PRI timestamp is `millis()` (ms since boot), not an RFC 3339 date — acceptable for LAN diagnostics but non-standard; a long-running device's syslog timestamps reset on every reboot and carry no date/time.
- The 576-byte `line` buffer (`src/sys/syslog.cpp:39`) is smaller than the RFC 5424 max 2 KB datagram — a very long caller message could be silently truncated by `snprintf` (which null-terminates, so it is safe, but the tail is lost).
- `sendto` return value is ignored (`src/sys/syslog.cpp:47`) — no retry, no error escalation on send failure (e.g. host unreachable).

## 18. Open Questions

1. Not determinable from the inspected source code — whether a live toggle of `syslogEnabled` triggers a `syslogInit()` re-call from the config-apply path; the inspected `sys/syslog.cpp` has no re-init entry point, and the config-apply handler (`src/cfg/config_core.cpp`) was not inspected.
2. Not determinable from the inspected source code — the full set of `syslogPrintf` callers; only `net_state.cpp:182` was found in the grep, but other modules may emit debug-level logs that were not searched.
3. Not determinable from the inspected source code — whether the syslog socket should be `SOCK_DGRAM | SOCK_NONBLOCK` to avoid blocking the calling task if the send buffer is full; the current `socket()` uses default (blocking) flags (`src/sys/syslog.cpp:19`).

## 19. Testing

- No host-native test covers `syslog.cpp` — `socket`, `sendto`, `WiFi`, and `IPAddress::fromString` are not shimmed in `test/native/`.
- The `pri = facility*8 + level` computation is a pure arithmetic expression that could be unit-tested but is not referenced in `test/native/*_test.cpp`.
- The RFC 5424 `<PRI>1 ...` framing is validated manually by sending a syslog message and inspecting the receiver output; no assertion-based test exists.
- `config_test.cpp` does not reference `syslogEnabled` or any syslog field.

## 20. History

- Syslog client extracted from inline `Serial.printf` spam in `main.cpp` during the 5-layer refactor, centralising remote-logging behind a `CFG_LIVE` flag (`src/cfg/config_schema.cpp:123`).
- `SyslogLevel` enum (RFC 5424 severity) defined in `syslog.h` (`src/sys/syslog.h:6-15`) to mirror the standard wire values 0–7, rather than the ad-hoc numeric constants used in v1.
- `syslogInit`/`syslogSend`/`syslogPrintf` split so that short messages can use `syslogSend` directly while `syslogPrintf` handles formatted strings — avoiding a va_arg race in the `sendto` path.
- Non-ESP32 (`#else`) `printf` echo path (`src/sys/syslog.cpp:28`) was added so the module compiles cleanly in the native test build even without a UDP socket.
