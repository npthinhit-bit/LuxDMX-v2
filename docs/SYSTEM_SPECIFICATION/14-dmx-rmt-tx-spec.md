# DMX RMT TX Driver Specification

Domain: drv.dmx-rmt-tx

## 1. Module Overview

The DMX RMT TX driver is the sole DMX512 transmit path in the firmware. It clocks a complete DMX frame (break + mark-after-break + start code + 512 slots) out of an ESP32 RMT peripheral entirely in hardware, independently of CPU scheduling on core 0. The driver encodes whatever 513-byte buffer it is handed by the caller into RMT symbol words and triggers hardware transmission; once kicked, the RMT peripheral streams the waveform autonomously and the CPU is free to service other outputs. The RDM engine reuses the same RMT channel for request transmission, bypassing the per-frame tick loop.

This is a leaf driver module. It owns the RMT TX channel lifecycle, a per-output symbol buffer, and a lazy-initialized byte-level run-length encoding lookup table. It delegates nothing to other modules and exposes no wire protocol — it only produces the DMX512 physical-layer waveform.

## 2. External Interfaces

### Caller-Facing API

| Function | Signature | Behavior |
|---|---|---|
| `rmtDmxInit` | `(RmtDmx*, int gpio, int rmtChannel)` | Creates the RMT TX channel on the given GPIO and channel, allocates the DRAM symbol buffer, creates a copy encoder, enables the channel, and primes it with a zero frame. Returns `bool`. |
| `rmtDmxIdle` | `(RmtDmx*)` | Non-blocking check: returns `true` if the RMT channel has finished its previous transmission. Returns `bool`. |
| `rmtDmxKick` | `(RmtDmx*, const uint8_t* data, int nslots)` | Encodes the 513-byte frame into RMT symbols, then transmits. Non-blocking — returns immediately after the hardware transmit is initiated. |
| `rmtDmxWait` | `(RmtDmx*)` | Blocking wait until the RMT channel is done. Ignores the result. |
| `rmtDmxSend` | `(RmtDmx*, const uint8_t*, int, int)` | Blocking convenience wrapper (init + encode + transmit + wait). Has no active callers in the current build. |
| `rmtDmxEncode` | `(RmtDmx*, const uint8_t*, int)` | Encodes a frame into the per-output symbol buffer. Called directly by the RDM engine for request TX. Returns word count (`int`). |

### Data Structures

| Struct | Fields | Description |
|---|---|---|
| `RmtDmx` | `chan`, `enc`, `sym`, `nsym`, `channel`, `hasDma`, `breakTime`, `mabTime`, `invert` | Per-output RMT transmit context. Stored as `dmx_output_t.rmt`. |
| `dmx_output_t` | `rmt` (RmtDmx), `dePin`, `uart`, `mode`, `ready`, ... | Per-output runtime struct; the RMT slice is the `rmt` field. |

### Lookup Table (file-scope static)

A single pair of 256-entry run-length-encoded lookup tables is built once and shared across all outputs. Normal polarity and inverted polarity variants exist. The idle flag guards one-time initialization.

### Hardware Characteristics

| Parameter | Value |
|---|---|
| RMT clock resolution | 1 MHz (1 tick = 1 us) |
| DMX bit period | 4 us (250 kbaud) |
| DMX packet size | 513 bytes (start code + 512 slots) |
| Max symbol words | 3000 per frame |
| Default break | 176 us |
| Default MAB | 12 us |

## 3. State Machine

This module has no internal state machine. Each call to `rmtDmxKick` independently re-encodes the entire frame and re-kicks the RMT. There is no retained transmission state across calls beyond what the RMT peripheral holds in hardware. The only per-call state is `nsym` (symbol count written by the most recent encode).

The RMT channel itself has two relevant hardware states:
- **Idle** — channel is free; a new `rmtDmxKick` can begin.
- **Transmitting** — channel is streaming the symbol buffer; `rmtDmxIdle` returns `false`.

## 4. Data Flow

1. **1 ms tick (core 1, `dmxTxTask`):** The task snapshot-and-transmit loop wakes.
2. **Buffer snapshot:** The seqlock-staged 513-byte DMX frame is copied into a stack-local buffer. If the snapshot fails after 8 retries (torn read), the frame is skipped.
3. **Idle check:** `rmtDmxIdle` polls whether the previous frame has completed. If still busy, the kick is skipped — this prevents fast and slow outputs from blocking each other.
4. **Encode:** `rmtDmxKick` calls `rmtDmxEncode`, which builds the byte LUT once on first use, writes a single break+MAB symbol word, then copies precomputed per-byte symbols from the selected normal or inverted lookup table.
5. **Transmit:** `rmt_transmit` is called with `loop_count=0` and `eot_level=1` (idle high = DMX mark). The RMT hardware streams the symbol buffer autonomously.
6. **Background:** The CPU returns to other outputs while the RMT ISR refills the 64-word channel memory from the symbol buffer.

### RDM Variant

The RDM engine calls `rmtDmxEncode` + `rmt_transmit` directly for RDM request transmission, then `rmt_tx_wait_all_done` with a 60 ms timeout, then toggles DE/RE GPIO and flushes the UART RX. This path runs on the dedicated core-1 RDM task (priority 18) and blocks briefly, which is acceptable because the RDM task does not share the timing-critical `dmxTxTask` tick.

## 5. Configuration Integration

This module reads no configuration fields directly. It is a pure hardware driver. Configuration is applied by its consumers:

| Config field | Consumer | CFG flag | Applied at |
|---|---|---|---|
| `breakTime` | Output init | CFG_LIVE | Init + runtime update |
| `mabTime` | Output init | CFG_LIVE | Init + runtime update |
| `invert` (polarity) | Output init | CFG_LIVE | Init + runtime update |
| `txPin` (GPIO) | Output init | CFG_REBOOT | Init only |
| `mode` (output mode) | Output init | CFG_REBOOT | Init only |
| `port`, `rxPin`, `rtsPin` | Output init | CFG_REBOOT | Init only |
| `txRate` | Task layer | CFG_LIVE | Live via rate table |
| `txStyle` | Task layer | CFG_LIVE | Live |
| `inputMode` | Input router | CFG_LIVE | Live |

Live config changes to break/MAB/invert are pushed into the running `RmtDmx` instance without reboot.

## 6. Lifecycle

| Phase | Function | Notes |
|---|---|---|
| Pre-init | `dmxInitGuardBegin` | NVS crash counter; may disable outputs before init. |
| Init | `outputInitAll` then `rmtDmxInit` per output | Per output: allocates symbol buffer, creates RMT channel + copy encoder, enables, primes with zero frame. Runs inside the crash-guard window. |
| Runtime | `rmtDmxIdle` then `rmtDmxKick` per 1 ms tick | Core 1 `dmxTxTask`. Non-blocking idle check only — no blocking wait in the live tick path. |
| RDM runtime | `rmtDmxEncode` + `rmt_transmit` + `rmt_tx_wait_all_done` | Core 1 RDM task. Blocking 60 ms wait acceptable here. |
| Update | Live config push | `updateOutputRuntime` pushes break/MAB/invert into the live `RmtDmx` without reboot. |
| Cleanup | None | No deinit function exists. The RMT channel is never released — by design, RDM reuses it. |

## 7. Error Handling

| Function | Return | Failure condition | Behavior |
|---|---|---|---|
| `rmtDmxInit` | `bool` | Symbol buffer allocation failure | Returns `false`; logs allocation failure |
| `rmtDmxInit` | `bool` | RMT channel creation failure (both DMA and non-DMA fallback) | Returns `false`; logs channel number + error |
| `rmtDmxInit` | `bool` | Copy encoder creation failure | Returns `false`; logs encoder error |
| `rmtDmxInit` | `bool` | RMT enable failure | Returns `false`; logs enable error |
| `rmtDmxKick` | `void` | `chan` is null | No-op |
| `rmtDmxKick` | `void` | `rmt_transmit` returns error | Silently ignored — no error return checked |
| `rmtDmxIdle` | `bool` | `chan` is null | Returns `false` |
| `rmtDmxIdle` | `bool` | Channel still transmitting | Returns `false` (caller skips kick) |
| `rmtDmxEncode` | `int` | Symbol buffer overflow (`wi + n >= MAX_SYM`) | Breaks slot loop silently, truncating the frame |

Errors are logged via `Serial.println`/`Serial.printf`, not `ESP_LOGE`. The 60 ms timeout in `rmt_tx_wait_all_done` bounds the RDM blocking call. The non-DMA fallback path on the classic ESP32 has a latent undefined-behavior issue (fall-through without explicit return).

## 8. Timing Constraints

| Constraint | Value | Basis |
|---|---|---|
| RMT clock resolution | 1 MHz | 1 tick = 1 us |
| DMX bit period | 4 us | 250 kbaud |
| Minimum break | 176 us (default) | E1.11 minimum is 88 us |
| Minimum MAB | 12 us (default) | E1.11 minimum is 4 us |
| DMX packet size | 513 slots | Start code + 512 channels |
| Frame air time | ~24.3 ms | 513 x 44 us + break + MAB |
| `dmxTxTask` tick | 1 ms | Hard real-time; priority 19 on core 1 |
| TX rate options | 25 / 24 / 30 / 40 / 50 ms | Indexed by config enum |
| RDM TX wait timeout | 60 ms | Bounds blocking transmit wait |
| RMT idle check timeout | 0 (non-blocking) | `rmt_tx_wait_all_done(chan, 0)` |
| Priming frame absorb | 200 ms | After init, waits for priming frame to drain |

The RMT hardware clocks the symbol stream autonomously; once `rmt_transmit` is called the CPU is not on the timing path for individual bits. The only CPU involvement after kick is the ISR refilling the 64-word channel memory from the symbol buffer as it drains.

## 9. Memory & Allocation Model

- **Symbol buffer (`sym`):** Allocated via `heap_caps_malloc` with `MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA` flags. Size: 3000 x `sizeof(rmt_symbol_word_t)`. Must be DRAM/DMA-capable for the RMT peripheral. The pointer is preserved across any deinit/re-init.
- **Lookup tables (file-scope static):** Two sets of 256-entry tables (normal and inverted polarity), each entry being an array of up to 6 RMT symbol words. Total ≈ 12.3 KB in BSS (zero-initialized, internal RAM, never PSRAM).
- **Per-output `RmtDmx`:** Embedded as `dmx_output_t.rmt`. Stored in a global output array. Not on PSRAM.
- **Task stack:** Transmits from the caller's stack. `dmxTxTask` has 8192 bytes; the encode path uses a 513-byte stack-local frame copy plus the symbol buffer in DRAM.

No heap allocation occurs at runtime — the symbol buffer is allocated once at init and never freed.

## 10. Safety Considerations

- **Core isolation:** This driver is consumed exclusively by `dmxTxTask` on core 1 (priority 19) and the RDM task (priority 18, also core 1). Core-0 network/WiFi activity never preempts the 1 ms DMX tick. This was the core fix for issue #64 — core-0 network DMA previously corrupted DMX break timing.
- **No locks needed:** The symbol buffer is owned by a single caller between `rmtDmxKick` and `rmtDmxIdle`. No seqlock is used inside this module; the upstream seqlock governs the `DmxBuffer` snapshot that produced the frame.
- **Benign failure on late refill:** If the RMT refill ISR is ever late, the peripheral idles the line (a benign extra mark) rather than emitting a corrupted break. This is a hardware-level safety property of using RMT instead of CPU bit-banging.
- **Non-blocking idle in the live tick path:** `rmtDmxIdle` uses a zero-timeout wait (`rmt_tx_wait_all_done(chan, 0)`), so a slow or oversized frame never blocks the 1 ms tick — it is simply skipped that cycle and retried on the next.
- **LUT built once:** The 256-entry byte lookup table is built lazily on the first encode and shared across all outputs. The guard is not atomic, but in practice only core 1 calls these functions, so no race exists.

## 11. Cross-Module Dependencies

| Module | Provides to this module | Consumes from this module |
|---|---|---|
| Output Init | Per-output config (pins, break/MAB/invert, mode) | — (leaf driver) |
| DMX Buffer | Seqlock-staged 513-byte frame snapshots | — |
| Sys Tasks | 1 ms tick cadence, `outReady` flags | `rmtDmxIdle`, `rmtDmxKick` |
| RDM Engine | — | `rmtDmxEncode` for RDM request TX |
| RDM Task | — (shares RMT channel on core 1) | — |
| Include headers | `dmx_output_t` struct containing `RmtDmx` | — |
| Include headers | — | `SeqLock` type (used upstream) |

This module depends only on the ESP-IDF RMT driver API and the `RmtDmx`/`dmx_output_t` struct definitions. It has no dependencies on the config, net, or app/sys layers.

## 12. Testing Verification

No host-native or hardware test coverage exists for this module. It is a hardware-bound RMT driver with no shim or host-test path. The only related native test verifies that the DMX packet size constant equals 513, a constant consumed by this module but defined in a separate header, which this module also uses but defines independently. No automated verification of symbol buffer overflow, idle-check behavior, or break/MAB timing is performed.

## 13. Open Questions

- Whether `rmt_transmit` returning an error is expected to propagate to stats counters or trigger a DE/RE re-init.
- Whether the 200 ms priming absorb is sufficient under worst-case WiFi coexistence on ESP32-S3.
- The rationale for omitting a `rmtDmxDeinit` — the comment references a historical architecture, but the current RDM design using a separate RX UART removes the need, yet this is not formally asserted.
- Whether `RMT_DMX_MAX_SYM = 3000` is empirically validated against the worst-case 513-byte frame at maximum run lengths.

## 14. History

- **Issue #64** (core separation, RMT hardware TX): RMT replaced the UART plus GPTimer DMX TX path to eliminate core-0 network-DMA-induced break corruption.
- **Issue #93** (multi-rate outputs): `rmtDmxIdle` non-blocking check was added so outputs at different TX rates never delay each other.
- **ESP32-S3 DMA fallback:** Added a `#if defined(SOC_RMT_SUPPORT_DMA)` block with fallback to non-DMA ISR refill so the 2nd RMT channel does not fail to initialize on S3 (only one TX channel has DMA).
- **RDM architecture decision:** The RDM engine uses low-level RMT primitives directly — it does not dispatch through the task layer for TX. This is the documented architectural decision for `rdm_architecture_dispatch_vs_lowlevel`.
