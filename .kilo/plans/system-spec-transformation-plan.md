# Plan: Black-Box System Specification for LuxDMX-v2

## Goal

Transform the 47 legacy technical reference documents in `docs/TECHNICAL_REFERENCE/` into a technology-
agnostic Black-Box System & Functional Specification under `docs/SYSTEM_SPECIFICATION/`, with `INDEX.md`
as the master TOC and 47 module spec files following a strict 8-section template.

## Context Gathered

All 47 reference files in `docs/TECHNICAL_REFERENCE/` have been read. The `CLAUDE.md`, `AGENTS.md`,
`platformio.ini`, and key source directories (`src/`, `include/`, `templates/`, `test/`) provide the
authoritative reference for grounding every spec claim. The build gate is `pio run -e esp32s3_psram`.

## Key Decisions

1. **Abstraction level**: No file paths, class names, function names, or line numbers unless they
   represent a hardware or external protocol boundary (e.g., UDP port 6454 for Art-Net). Internal
   implementation details MUST be stripped.

2. **Template**: Every spec file MUST follow exactly 8 sections:
   1. Module Overview
   2. Functional Requirements
   3. Data Structures
   4. State Machines
   5. Interfaces
   6. Timing
   7. Error Handling
   8. Cross-Module Dependencies

3. **Normative language**: RFC 2119 keywords (MUST/MUST NOT/SHOULD/SHOULD NOT/MAY/RECOMMENDED).

4. **INDEX.md**: Already contains the 5-layer model, core isolation strategy, cross-core primitives
   table, data-flow narratives, protocol summary, config model, build gate, spec plan table (47
   files), and legend.

## Execution Order

### Phase 1: Foundation
1. `01-system-architecture-spec.md` — system.architecture (core affinity, layering, boot sequence)

### Phase 2: Configuration Layer
2. `02-config-engine-spec.md` — cfg.config-engine (schema table, resolution order, NVS, serial, live/reboot)

### Phase 3: Core Layer
3. `03-dmx-buffer-spec.md` — core.dmx-buffer (SeqLock DMX frame, ArtSync/sACN staging)
4. `04-merge-engine-spec.md` — core.merge-engine (HTP/LTP/priority modes, loss modes)
5. `05-sender-tracker-spec.md` — core.sender-tracker (source caching, priority, timestamps)
6. `06-frame-router-spec.md` — core.frame-router (universe routing, splitMask, output mapping)
7. `07-output-init-spec.md` — core.output-init (outputInitAll, sanitizeOutputs, crash-guard)
8. `08-scene-engine-spec.md` — core.scene-engine (scene recall, fade)
9. `09-rdm-engine-spec.md` — core.rdm-engine (E1.20 transport, RMT TX, UART RX)
10. `10-rdm-discovery-spec.md` — core.rdm-discovery (DISC_UNIQUE_BRANCH, UID sorting)
11. `11-rdm-task-spec.md` — core.rdm-task (RDM task dispatch, transaction scheduling)
12. `12-input-router-spec.md` — core.input-router (DMX input polling, port mapping)
13. `13-stats-spec.md` — core.stats (frame counting, input statistics)

### Phase 4: Driver Layer
14. `14-dmx-rmt-tx-spec.md` — drv.dmx-rmt-tx (RMT hardware clocking, 250 kbaud)
15. `15-dmx-uart-rx-spec.md` — drv.dmx-uart-rx (UART RX, break detection, 2 ms timeout)
16. `16-gpio-dir-spec.md` — drv.gpio-dir (DE/RE GPIO control)

### Phase 5: Network Layer (Part 1 — Protocols)
17. `17-artnet-protocol-spec.md` — net.artnet (opcode dispatch, ArtSync staging)
18. `18-sacn-protocol-spec.md` — net.sacn (multicast, Stream-Sync staging)
19. `19-artnet-bridge-spec.md` — net.artnet-bridge (control opcodes: Poll, Address, IPProg, RDM, TodRequest)
20. `20-art-packet-queue-spec.md` — net.art-pkt-queue (Art-Net packet ring buffer)
21. `21-art-rdm-resp-queue-spec.md` — net.art-rdm-resp-queue (SPSC cross-core ring)
22. `22-sacn-packet-queue-spec.md` — net.sacn-pkt-queue (sACN packet ring buffer)

### Phase 6: Network Layer (Part 2 — Web & OTA)
23. `23-websocket-protocol-spec.md` — net.websocket (binary push ~10 Hz, JSON commands)
24. `24-websocket-handler-spec.md` — net.websocket-handler (event handling)
25. `25-web-server-spec.md` — net.web-server (HTTP/HTTPS server)
26. `26-web-routes-spec.md` — net.web-routes (REST endpoints: /info, /config, /ota)
27. `27-web-frontend-spec.md` — net.web-frontend (HTML UI, placeholder substitution)
28. `28-rate-limiter-spec.md` — net.rate-limiter (per-IP OTA rate limiting)
29. `29-ota-spec.md` — net.ota (OTA boot-retry, HTTP multipart upload, GitHub releases streaming)
30. `30-ota-sign-spec.md` — net.ota-sign (Ed25519 signature, SHA-256 hash)

### Phase 7: Network Layer (Part 3 — Connectivity)
31. `31-ethernet-spec.md` — net.ethernet (RMII/W5500 Ethernet)
32. `32-net-state-spec.md` — net.net-state (WiFi state management)
33. `33-setup-portal-spec.md` — net.setup-portal (WiFi setup portal)

### Phase 8: System Layer
34. `34-tasks-spec.md` — sys.tasks (FreeRTOS task lifecycle, scheduling table)
35. `35-crash-guard-spec.md` — sys.crash-guard (progressive output disabling, NVS counter)
36. `36-led-status-spec.md` — sys.led-status (LED animations, PWM)
37. `37-display-spec.md` — sys.display (OLED/SPI display render)
38. `38-soak-monitor-spec.md` — sys.soak-monitor (heap watchdog, conditional build)
39. `39-firmware-version-spec.md` — sys.firmware-version (version constants, update check)
40. `40-syslog-spec.md` — sys.syslog (RFC 5424 UDP syslog)
41. `41-alert-spec.md` — sys.alert (webhook alerts, per-output latch)

### Phase 9: App Layer & Cross-Cutting
42. `42-enc-decode-spec.md` — app.enc-decode
43. `43-input-map-spec.md` — app.input-map
44. `44-menu-spec.md` — app.menu
45. `45-include-headers-spec.md` — include (cross-cutting types/structs)

### Phase 10: Infrastructure
46. `46-build-system-spec.md` — system.build (PlatformIO, 5 environments, code generation)
47. `47-test-infrastructure-spec.md` — system.test (native smoke tests, Unity unit tests)

## Critical Constraints

- **Build gate**: After creating each batch of files (or at minimum before finalizing), run
  `pio run -e esp32s3_psram` to ensure the build remains green. This is a verification step, not an
  edit gate — the spec files are documentation and do not affect the build, but the constraint from
  `project.md_Constraints_build_gate` must be respected.
- **No implementation changes**: This plan creates documentation only. No source code is modified.
- **Existing INDEX.md**: Already written at `docs/SYSTEM_SPECIFICATION\INDEX.md` — verify it is
  complete and accurate against the reference docs.
- **Cross-layer dependencies**: Each spec's "Cross-Module Dependencies" section MUST list upstream
  and downstream components by their layer name (not file names).

## Validation Steps

1. Verify `INDEX.md` exists, all 47 file references in the plan table match the actual created files.
2. Spot-check 3 spec files for adherence to the 8-section template and absence of implementation
   details (file paths, class names, line numbers).
3. Confirm all RFC 2119 keywords are used correctly (MUST = absolute requirement).
4. Confirm the data-flow narratives in INDEX.md are consistent with the corresponding module specs.
5. Run `pio run -e esp32s3_psram` to confirm build integrity is unaffected (documentation-only
   changes).
6. Verify no file under `docs/SYSTEM_SPECIFICATION/` references a source file path, class name, or
   function name from the implementation — except for protocol/port boundaries (e.g., "Art-Net
   UDP port 6454").

## Deliverable

A complete `docs/SYSTEM_SPECIFICATION/` directory containing `INDEX.md` and 47 module spec files,
all following the 8-section template with technology-agnostic abstraction and RFC 2119 normative
language.
