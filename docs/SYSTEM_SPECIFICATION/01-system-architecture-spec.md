# System Architecture Specification

Domain: system.architecture

## 1. Module Overview

The LuxDMX V2 firmware is an Art-Net / sACN (E1.31) to DMX512 gateway for ESP32 and ESP32-S3 embedded boards. The firmware is organized as a thin wiring layer that boots the device and starts tasks; all application logic resides in five architectural layers arranged in a directed dependency chain. The architecture is optimized for hard real-time DMX output: network and serial processing never preempt the 1 ms DMX transmit tick.

The layering model is:

```
drv -> cfg -> core -> net -> app/sys   (wired by the thin entry layer)
```

- **drv** — low-level hardware drivers: RMT DMX transmit, UART DMX input, GPIO DE/RE direction control.
- **cfg** — schema-driven configuration engine: a single field table drives NVS persistence, serial console grammar, and the web configuration form.
- **core** — DMX merge engine, seqlock-protected frame buffers, sender tracking, RDM E1.20 controller, scene engine.
- **net** — self-implemented Art-Net / sACN protocols, AsyncWebServer, WebSocket, Ethernet bring-up, OTA.
- **sys/app** — FreeRTOS tasks, LED/display status, crash-guard, soak test monitor, syslog, firmware version.

No business logic resides in the entry layer. It is call-sequence glue that brings up hardware, initializes configuration, and spawns the RTOS task graph.

## 2. External Interfaces

### Network Protocols

| Protocol | Port | Direction | Purpose |
|---|---|---|---|
| Art-Net 4 | UDP 6454 | Bidirectional | DMX universe transport, RDM, sync, device management |
| sACN (E1.31) | UDP 5568 | Multicast in | DMX universe transport, stream sync, universe discovery |
| WebSocket | TCP 80 / WSS | Bidirectional | Live DMX channel push (~10 Hz), JSON command channel |
| HTTP | TCP 80 | In | Static web UI, REST config API, OTA endpoints |
| mDNS | UDP 5353 | In | Hostname advertisement (dmx-gateway.local) |
| Syslog | UDP 514 | Out | RFC 5424 structured logging |

### Hardware Interfaces

| Interface | Signal | Purpose |
|---|---|---|
| RMT peripheral | GPIO (TX) | DMX512 waveform timing (break, MAB, slot data) |
| UART (RX-only) | GPIO (RX) | DMX input framing, RDM response reception |
| UART0 | Serial | Console / serial config terminal |
| GPIO | DE/RE | RS485 transceiver direction control |
| SPI | W5500 CS/MDC/MDIO | Ethernet MAC/PHY (W5500 SPI variant) |
| RMII | Ethernet RMII signals | Ethernet PHY (LAN8720 variant) |

### Serial Console

A line-oriented text command parser on UART0 supporting dump, set, get, save, wifi, reboot, and help verbs. Commands map directly to the configuration schema table.

## 3. State Machine

The firmware has no single global state machine. Each subsystem maintains independent state:

| Subsystem | States |
|---|---|
| Config Engine | Neutral -> Template -> NVS Overlay |
| Network | Boot -> WiFi/AP/Ethernet -> Connected -> (Setup Portal on failure) |
| DMX TX | Idle -> Snapshot -> RMT Encode -> RMT Transmit -> Idle (1 ms cycle) |
| RDM Task | Idle -> Command Queue -> RMT TX -> UART RX (9 ms window) -> Response -> Idle |
| OTA | Idle -> Downloading -> Verifying -> Error / Commit |
| Scene Engine | Idle -> Fade (1 ms step) -> Idle |
| Link Loss | Configured -> Link Lost -> Policy Applied (Retry / AP / Reboot / WiFi) |

System-wide startup follows a numbered phase sequence (see Lifecycle).

## 4. Data Flow

### Network Input to DMX Output

1. `netRxTask` (core 0) drains UDP on the Art-Net (6454) and sACN (5568) sockets. Art-Net receive is bounded to 8 packets per call; sACN is bounded to 4 sockets.
2. Incoming ArtDMX frames are parsed (universe, data, length) and routed to `routeFrame`. Incoming sACN frames follow the same `routeFrame` path with a different protocol discriminator.
3. `routeFrame` writes the 513-byte frame into the per-output seqlock-staged buffer. The writer bumps a 32-bit seqlock ticket (odd = mid-write).
4. `dmxTxTask` (core 1) wakes every 1 ms, snapshots the buffer via the seqlock reader. If the ticket changed or was odd during the copy, the snapshot is retried (up to 8 attempts). Torn reads are detected and skipped, never transmitted.
5. The snapshot is encoded into RMT symbols and transmitted in hardware. The CPU refills the RMT symbol memory from a precomputed buffer; once kicked, the RMT peripheral streams autonomously.

### Art-Net RDM Async Response Path (Cross-Core)

1. Core 0 `netRxTask` receives an ArtRdm packet.
2. `handleArtRdm` enqueues a command via a non-blocking cross-core queue send, carrying the request bytes, source IP, and target output line index.
3. Core 1 RDM task dequeues the command and selects the active RMT/UART/GPIO line for the requested output.
4. The RDM engine transmits the request over RMT, toggles DE/RE to listen, and reads the response on the dedicated RX UART within a 9 ms timeout.
5. The completed response is pushed into a cross-core response ring.
6. Core 0 `netRxTask` drains the response ring and sends the reply via UDP 6454 back to the requester.

### Configuration Resolution

1. Template defaults are applied from the per-board template file (with inheritance via a base template).
2. NVS overlay is applied on top of the template, overriding any persisted user settings.
3. Resolution order: neutral constraint value -> active board template -> saved NVS value.

## 5. Configuration Integration

All persisted settings are defined in a single field table. Each field carries a `CFG_LIVE` or `CFG_REBOOT` flag:

- **CFG_LIVE** — applies instantly via web UI or serial console: universe, merge mode, loss mode, TX rate, TX style, LED brightness, protocol, timecode, syslog, webhook, RDM, DSCP.
- **CFG_REBOOT** — requires restart: LED pin/type, DMX TX/RX/RTS pins, UART port, network mode, display config, Ethernet CS/MDC/MDIO.

The template values provide the neutral default; NVS provides the runtime overlay. Live fields are pushed into running modules at runtime; reboot fields are applied during `setup()`.

## 6. Lifecycle

1. **NVS migration** — One-shot V1->V2 key migration in the `"dmxgw"` namespace.
2. **Config load** — Apply template defaults, then overlay NVS-saved values.
3. **Crash guard begin** — Read the NVS crash counter; if non-zero, progressively disable the lowest-indexed outputs before init to isolate a bad pin.
4. **Output init** — Initialize RMT TX channels, UART RX, and DE/RE GPIO for each enabled output, inside the crash-guard window.
5. **Crash guard end** — Write incremented counter, wait 3 seconds for stability; if the device survives, reset the counter to 0.
6. **Network bring-up** — Start WiFi station/AP or RMII/SPI Ethernet. Register mDNS services for HTTP, Art-Net, and sACN.
7. **OTA boot update** — Resume any deferred OTA install from the previous boot.
8. **Web server + WebSocket** — Register HTTP routes, start the WebSocket endpoint.
9. **Task spawn** — Create FreeRTOS tasks with core pinning.
10. **Loop** — Poll serial console, process dirty config flags, drain RDM/OTA queues, push WebSocket status frames.

## 7. Error Handling

| Subsystem | Mechanism |
|---|---|
| Config Engine | Return-code + out-parameter error strings; invalid fields rejected at load time |
| Art-Net | Socket init guard; packet dispatch checks header validity |
| Ethernet | 15-second link timeout, 30-second bring-up timeout |
| Crash Guard | NVS-backed counter; progressive output disable to recover from a panicked pin init |
| OTA | Boot attempt counter; auto-revert on signature verification failure |
| RDM | 9 ms response timeout; silent retry on read failure |
| Seqlock | 8 retry attempts; snapshot returns failure, frame skipped (never corrupted) |
| OTA | Three install paths (GitHub release, URL fetch, local upload) queue through a severity-gated background policy |

## 8. Timing Constraints

| Path | Period | Deadline | Notes |
|---|---|---|---|
| `dmxTxTask` tick | 1 ms | Hard real-time | Core 1, priority 19. All DMX merge, scene fades, ArtSync commit happen here |
| `netRxTask` tick | 2 ms | Bounded UDP drain | Core 0, priority 5. <=8 Art-Net packets, <=4 sACN sockets per call |
| DMX frame air time | ~24.3 ms | 44 us/slot | 513 slots x 44 us + break + MAB |
| RDM TX to RX turnaround | ~3 ms | 9 ms response window | RMT TX + 90 us DE toggle + UART RX |
| RDM discovery | 8 s budget total | Binary search cap | DISC_UNIQUE_BRANCH search |
| ArtSync commit grace | 1000 ms | Fallback timeout | If sync not received, staged frames commit immediately |
| sACN Stream Sync grace | 500 ms | Sync loss timeout | Staged frames commit after sync loss timeout |

## 9. Memory & Allocation Model

- **DRAM (internal):** RMT symbol buffers (heap_caps_malloc, DMA-capable), seqlock frame snapshot copies, task stacks (8-12 KB each), UART driver RX buffers.
- **PSRAM (external, 8 MB on PSRAM builds):** Scene preset arrays, WiFi/lwIP internal buffers.
- **Static/BSS:** All ring buffers (ArtNet SPSC ring, RDM response ring, sACN ring), global Config struct, LED state, sender table, RDM engine state.
- **ROM/PROGMEM:** Gzipped HTML pages, CSS, images, generated config templates.
- **Stack:** `dmxTxTask` 8192, `netRxTask` 8192, `versionCheckTask` 12288, `ledTask` 2048, `displayTask` 4096.

## 10. Safety Considerations

- **Core isolation:** AsyncTCP is pinned to core 0 (via build flags) with a 16 KB stack and queue depth 128. This isolates the core-1 DMX/RDM timing path from WiFi/lwIP contention. Measured on the HIL bench: with AsyncTCP on core 1, RDM TX-to-RX turnaround inflated 3-4x (median 15->53 us, max 126 us).
- **Seqlock for frame integrity:** Network-written DMX buffers are read by the transmit task via a seqlock. Torn reads are detected and skipped — never transmitted as corrupted data.
- **Crash guard:** An NVS-backed counter progressively disables the lowest-indexed output on each consecutive crash during output initialization, preventing a bad pin from bricking the device. A 3-second stable-uptime window resets the counter.
- **RMT benign failure mode:** If the RMT refill ISR is ever late, the peripheral idles the line (benign extra mark) rather than corrupting the DMX break — a deliberate design choice using hardware timing instead of CPU-driven UART bit-banging.
- **DE/RE GPIO never released:** The RMT TX channel and DE/RE GPIO are shared between DMX TX and RDM request TX. The DE/RE pin stays HIGH (drive) between RDM transactions, so DMX output is never interrupted during normal operation.

## 11. Cross-Module Dependencies

```
drv ----------------+
                    v
cfg (schema) -+--> cfg (serial/web)
                    v
core (buffer) <-- cfg (output fields)
core (merge) <-- net (frames)
core (rdm) <-- net (ArtRdm requests)
core (scene) --> core (merge, buffer)
                    v
net (protocol) <-- cfg
net (web) <-- cfg
                    v
sys (tasks) --> drv
sys (tasks) --> core
sys (tasks) --> net
sys (tasks) --> cfg
app/sys (main) --> all layers
```

The two cross-core data paths are the seqlock-staged DMX buffer (core 0 writes, core 1 reads) and the RDM response ring (core 1 writes, core 0 reads).

## 12. Testing Verification

Host-side tests (no hardware required):

| Test | Coverage |
|---|---|
| `config_test` | Template resolution, setValue/getValue, NVS save/load round-trip, serial console grammar |
| `seqlock_test` | Snapshot during write, stable write copy, 100 read-write cycles |
| `merge_test` | HTP, OFF/LTP, LOSS_ZERO, cross-universe isolation, LTP-Takeover priority, priority merge, failsafe timeouts |
| `rdm_types_test` | UID pack/unpack, enums, PID constants |

Web E2E tests (Playwright, requires a live device): drive real Art-Net/sACN packets, REST API, WebSocket, and web UI.

The 5-minute firmware evaluation workflow monitors serial output for task startup messages and heap stability.

## 13. Open Questions

- The implementation of `sanitizeOutputs()` (called during setup) is not fully visible in the inspected source — its internal validation rules for per-output pin sets are not determinable.
- Whether a WiFi disconnection/reconnection handler exists after initial connection (only `startWiFiStation` is visible in the inspected source).
- The full sACN socket read implementation body and the exact Stream Sync timeout enforcement are not fully determinable from inspected source.
- The `g_dmxTask` handle is captured at task creation but its intended use for inter-task notification is not visible in the inspected source.

## 14. History

- Core pinning strategy: AsyncTCP pinned to core 0, RDM to core 1, to isolate real-time DMX timing from WiFi/lwIP preemption.
- RMT replaced UART-based DMX TX (the original issue #64 root cause was core-0 network DMA corrupting DMX break timing).
- Seqlock replaced mutex-based buffer access to provide single-copy consistent snapshots without blocking either core.
- RDM moved to a dedicated core-1 task (priority 18, below the DMX TX task at priority 19) so RDM transactions never block the 1 ms frame tick.
- Crash guard introduced NVS-backed progressive output disable to prevent brickage from bad pin initialization.
