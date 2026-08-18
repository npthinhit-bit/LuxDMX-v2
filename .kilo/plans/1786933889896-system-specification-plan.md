# Plan: Black-Box System Specification for LuxDMX-v2

## Objective
Transform the legacy technical reference docs in `docs/TECHNICAL_REFERENCE/` into a technology-agnostic Black-Box System Technical & Functional Specification in `docs/SYSTEM_SPECIFICATION/`, creating `INDEX.md` and `01-system-architecture-spec.md` first.

## Constraints
1. **8-section template** for every spec file: Module Overview → Functional Requirements → Data Structures → State Machines → Interfaces → Timing → Error Handling → Cross-Module Dependencies.
2. **RFC-style normative language** (MUST/MUST NOT/SHOULD/SHOULD NOT/MAY/RECOMMENDED).
3. **No implementation details**: no file paths, class names, function names, or source line numbers unless representing hardware/protocol boundaries (e.g., "Art-Net UDP/6454", "RMT peripheral").
4. **5-layer architecture** is retained as the structural model: drv → cfg → core → net → app/sys, wired by main entry point.
5. **Core pinning** is described as a system constraint: Core 0 = network receive (priority 5), Core 1 = DMX transmit + RDM (priority 19). SeqLock for cross-core buffer handoff.
6. **Build gate**: Every source change must pass `pio run -e esp32s3_psram` — though this task is docs-only, no source changes are expected.

## Deliverables
1. `docs/SYSTEM_SPECIFICATION/INDEX.md` — Master table of contents with layer map, data flow summary, config resolution order, and build gate note.
2. `docs/SYSTEM_SPECIFICATION/01-system-architecture-spec.md` — The foundational architecture spec covering: 5-layer model, core isolation strategy, concurrency model, data flow paths (network→DMX, Art-Net RDM async path, config resolution), protocol summary (Art-Net, sACN, WebSocket), lifecycle, error handling, memory allocation, timing constraints.

## Key Abstractions (Technology-Agnostic)
- **DMX Output**: A physical DMX512-A port driven by an RMT hardware peripheral on Core 1.
- **Sender**: An inbound network source (Art-Net or sACN) identified by IP+protocol.
- **Merge Engine**: Per-output logic selecting which sender(s) contribute based on merge mode and priority.
- **SeqLock Buffer**: Lock-free cross-core (Core 0 writer, Core 1 reader) 513-byte DMX frame buffer.
- **Config Engine**: Schema-driven single-source-of-truth for all settings; NVS persistence; live vs reboot config semantics.
- **RDM Controller**: E1.20 transport using RMT-TX for requests and a dedicated RX-only UART for responses, running on Core 1 at priority 18.
- **OTA Pipeline**: Ed25519-signed firmware updates with boot-retry crash guard.
- **WebSocket Interface**: Binary status/DMX push (~10 Hz) and JSON command channel.

## Key Enumerations (to carry into spec)
- **Merge modes**: MERGE_OFF, MERGE_HTP, MERGE_LTP, MERGE_LTP_TAKEOVER, MERGE_PRIORITY.
- **Loss modes**: LOSS_HOLD, LOSS_ZERO, LOSS_STOP, LOSS_PRESET, LOSS_HOME.
- **TX styles**: TXSTYLE_CONTINUOUS, TXSTYLE_DELTA.
- **Output modes**: OUTPUT_MODE_DMX_ONLY, OUTPUT_MODE_RDM_FULL.
- **WiFi modes**: NET_WIFI_STA, NET_WIFI_AP.
- **Link loss fallback**: WIRED_FB_RETRY, WIRED_FB_AP, WIRED_FB_REBOOT, WIRED_FB_WIFI.

## Timing Summary (to carry into spec)
| Path | Period | Deadline |
|---|---|---|
| DMX TX tick | 1 ms | 44 µs/frame |
| Network RX tick | 2 ms | All UDP within 2 ms |
| RDM TX→RX turnaround | ~3 ms | No preemption on Core 1 |
| RDM response timeout | 9 ms | UART RX window |
| RDM discovery budget | 8 s | DISC_UNIQUE_BRANCH search |
| ArtSync commit grace | 1000 ms | |
| sACN Stream Sync grace | 500 ms | Sync loss timeout |
| Crash guard stable window | 3000 ms | |

## Risks
- Risk of accidentally including implementation details (file paths, line numbers). Mitigation: review each section against the "no code details" rule before finalizing.
- Risk of missing some subsystem. Mitigation: the INDEX.md will list all planned future spec files as a roadmap, and the architecture spec will cover the high-level integration points.

## Validation
- Each spec file must follow the exact 8-section structure.
- No source file paths or line numbers may appear in the specifications (except hardware/protocol boundaries).
- The architecture spec must cover all major data flow paths and cross-layer dependencies.
