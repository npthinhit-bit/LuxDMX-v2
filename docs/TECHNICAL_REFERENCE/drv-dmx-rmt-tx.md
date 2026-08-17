# DMX RMT TX — Technical Reference

Domain: drv.dmx-rmt-tx

## 1. Domain Scope

This module is the **only** DMX512 transmit path in the firmware. It clocks a complete DMX frame (break + mark-after-break + start code + 512 slots) out of the ESP32 **RMT peripheral** in hardware, entirely independently of CPU scheduling on core 0. It owns nothing about where frame data comes from — it merely encodes and transmits whatever 513-byte buffer it is handed by the caller. Frame *content* and *timing of when to send* are decided upstream by `src/sys/tasks.cpp`. RDM request framing (`src/core/rdm_engine.cpp`) reuses the same RMT channel and the same encoder but bypasses the per-frame tick loop directly.

Consumers of this module:

| Consumer | File | What it calls |
|---|---|---|
| Output init | `src/core/output_init.cpp` | `rmtDmxInit` at line 71 |
| DMX TX task | `src/sys/tasks.cpp` | `rmtDmxIdle` line 102, `rmtDmxKick` line 103 |
| RDM engine | `src/core/rdm_engine.cpp` | `rmtDmxEncode` line 103, direct `rmt_transmit`/`rmt_tx_wait_all_done` lines 104–106 |

This module delegates all UART RX, GPIO DE/RE direction control, and frame-content decisions to other modules.

## 2. Layer Mapping

| Layer | Module path | Role here |
|---|---|---|
| **drv** | `src/drv/dmx_rmt.h` | RMT peripheral driver — hardware timing-critical transmit |
| **core** | `src/core/output_init.cpp` | Calls `rmtDmxInit` during `outputInitAll()`; configures per-output break/MAB/invert from `cfg` |
| **core** | `src/core/rdm_engine.cpp` | Calls `rmtDmxEncode` + direct `rmt_transmit` for RDM request TX |
| **sys** | `src/sys/tasks.cpp` | Calls `rmtDmxIdle`/`rmtDmxKick` from `dmxTxTask` on core 1 |
| **app/sys** | `src/main.cpp` | `outputInitAll()` (line 107) runs during setup, after which `dmxTxTask` (line 107→tasks.cpp:83) takes ownership |

## 3. Source Files

| File | Role |
|---|---|
| `src/drv/dmx_rmt.h` | Entire module — RMT TX channel lifecycle, symbol LUT, frame encoder, transmit primitives. Header-only (`static` / `static inline` functions). |
| `include/output.h` | Defines `dmx_output_t.rmt` (line 17) — the `RmtDmx` instance per output that this module operates on. |
| `include/seqlock.h` | Seqlock used to snapshot frame data into the transmit buffer before calling `rmtDmxKick`. |

No `.cpp` file exists for this module; all functions are `static` / `static inline` in the header and are inlined into their call sites at compile time.

## 4. Data Structures

**`RmtDmx`** — `src/drv/dmx_rmt.h:31-41`: per-output RMT transmit context.

| Field | Type | Line | Description |
|---|---|---|---|
| `chan` | `rmt_channel_handle_t` | 32 | IDF RMT TX channel handle; `nullptr` until `rmtDmxInit` succeeds. |
| `enc` | `rmt_encoder_handle_t` | 33 | Copy-encoder handle created via `rmt_new_copy_encoder`. |
| `sym` | `rmt_symbol_word_t*` | 34 | Symbol buffer in DRAM, allocated with `heap_caps_malloc(..., MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)`. |
| `nsym` | `int` | 35 | Count of valid symbols written into `sym` by the most recent encode. |
| `channel` | `int` | 36 | RMT TX channel number (0–3). |
| `hasDma` | `bool` | 37 | `true` if the channel was opened with `with_dma = true`. |
| `breakTime` | `uint16_t` | 38 | Break duration in microseconds (default `RMT_DMX_BREAK_DEFAULT` = 176 at line 22). |
| `mabTime` | `uint16_t` | 39 | Mark-after-break duration in microseconds (default `RMT_DMX_MAB_DEFAULT` = 12 at line 23). |
| `invert` | `bool` | 40 | If `true`, DMX polarity is inverted (break becomes high, MAB becomes low). |

**LUT tables** — `src/drv/dmx_rmt.h:49-53`: file-scope static arrays (not per-instance):

| Table | Line | Element size | Count | Purpose |
|---|---|---|---|---|
| `g_byteLut` | 49 | `rmt_symbol_word_t[RMT_MAX_WORDS_PER_BYTE]` | 256 | Precomputed run-length symbols for each byte value, normal polarity |
| `g_byteLutN` | 50 | `uint8_t` | 256 | Word count for each entry in `g_byteLut` |
| `g_byteLutInv` | 51 | same as `g_byteLut` | 256 | Same as `g_byteLut` but with levels swapped for inverted polarity |
| `g_byteLutInvN` | 52 | `uint8_t` | 256 | Word count for each entry in `g_byteLutInv` |
| `g_lutReady` | 53 | `bool` | 1 | Guard so `rmtDmxBuildLut()` runs once (lazy init at line 84). |

**`dmx_output_t`** — `include/output.h:16-26`: the per-output runtime struct; the RMT-specific slice is the `RmtDmx rmt` field at line 17. This struct is instantiated as `g_outputs[MAX_OUTPUTS]` in `src/core/output_init.cpp:14`.

## 5. Concurrency

**Core 1 only.** This module is consumed almost exclusively by `dmxTxTask`, which is pinned to core 1 at priority 19 (`src/sys/tasks.cpp:83`). The RMT peripheral and its refill ISR are therefore never contended by core-0 network/WiFi activity — the central problem issue #64 describes (`src/drv/dmx_rmt.h:1-9`). RDM request TX also originates on core 1 via the dedicated RDM task at priority 18 (`src/core/rdm_task.cpp:155-157` / `src/core/rdm_task.h:13`).

No mutexes, critical sections, or FreeRTOS primitives are used within this module itself. The `sym` buffer is owned by a single caller between `rmtDmxKick()` and `rmtDmxWait()`/`rmtDmxIdle()` — the caller must not reuse or free it until the channel reports done (`src/drv/dmx_rmt.h:169-170, 185-189`). No seqlock is used inside this module; the seqlock at `include/seqlock.h` governs the upstream `DmxBuffer` snapshot that produces the 513-byte frame fed to `rmtDmxKick` (`src/sys/tasks.cpp:99-103`, `src/core/dmx_buffer.h:31-40`).

## 6. State Machine

No state machine — stateless request/response. Each call to `rmtDmxKick()` independently re-encodes the entire 513-byte frame into `rd->sym` and re-kicks the RMT. There is no internal transmission state retained across calls beyond what the RMT peripheral itself holds in hardware. The `RmtDmx.nsym` field (`src/drv/dmx_rmt.h:35`) is the only per-call state, set fresh by every `rmtDmxEncode()` call.

## 7. Entry Points

Three entry points are called from outside the header:

| Entry point | First call site | Line | Trigger context |
|---|---|---|---|
| `rmtDmxInit(RmtDmx*, int gpio, int rmtChannel)` | `outputInitAll()` | `src/core/output_init.cpp:71` | One-shot during `outputInitAll()`, once per enabled output, inside the crash-guard window `dmxInitGuardBegin()/End()` (main.cpp:106-108). |
| `rmtDmxIdle(RmtDmx*)` | `snapshotAndTransmit()` | `src/sys/tasks.cpp:102` | Polled every 1 ms tick by `dmxTxTask` before every kick, to avoid jitter between outputs at different rates. |
| `rmtDmxKick(RmtDmx*, const uint8_t* data, int nslots)` | `snapshotAndTransmit()` | `src/sys/tasks.cpp:103` | Same 1 ms tick, after `dmxBufSnapshot` succeeds. |

Additionally, RDM request framing calls `rmtDmxEncode` + direct `rmt_transmit` at `src/core/rdm_engine.cpp:103-105`. The blocking convenience wrapper `rmtDmxSend()` (`src/drv/dmx_rmt.h:197`) has no callers in the inspected sources.

## 8. Data Flow

1. **`dmxTxTask` (core 1, 1 ms tick)** begins a frame cycle at `src/sys/tasks.cpp:138-144`, calling `dmxFrameTick()` (line 141).
2. **`dmxFrameTick`** iterates outputs at `src/sys/tasks.cpp:127-134`, checking `cfg.outputs[i].enabled && outReady[i]` (line 128) and per-output period `dmxPeriodMs(i)` (line 130).
3. **`snapshotAndTransmit`** is called per due output at `src/sys/tasks.cpp:97-108`. It first checks `cfg.outputs[outIdx].txStyle == TXSTYLE_DELTA && !stats().outSrcLost[outIdx]` (line 98) — delta-style outputs skip transmit when the source is still live.
4. **`dmxBufSnapshot(outIdx, frame)`** copies the seqlocked frame into a stack-local 513-byte buffer at `src/core/dmx_buffer.cpp:7-17` / `src/core/dmx_buffer.h:44`. Returns `false` on 8 consecutive torn-read retries, in which case step 3 returns early without transmitting (`src/sys/tasks.cpp:100`).
5. **`rmtDmxIdle(rd)`** non-blocking checks whether the previous frame finished at `src/drv/dmx_rmt.h:190-193`. If the channel is still busy, the kick is skipped so a slow/oversized frame does not delay sibling outputs at different rates (`src/sys/tasks.cpp:102-103`, `src/drv/dmx_rmt.h:185-189`).
6. **`rmtDmxKick(rd, frame, DMX_PACKET_SIZE)`** encodes the 513 bytes at `src/drv/dmx_rmt.h:172-179`: calls `rmtDmxEncode()` (step 7), then `rmt_transmit()` with `loop_count=0`, `eot_level=1` (idle high = DMX mark).
7. **`rmtDmxEncode(rd, data, nslots)`** at `src/drv/dmx_rmt.h:83-107`: if the LUT is not yet built (`g_lutReady` at line 53), calls `rmtDmxBuildLut()` (line 84, builds the 256-entry byte LUT once). Writes a break+MAB word (line 96), then `memcpy`-style copies precomputed words for each byte from the selected normal or inverted LUT (lines 100-104). Sets `rd->nsym` (line 105). Returns word count.
8. **RMT hardware** streams `rd->sym` out on the configured GPIO autonomously. The IDF RMT driver ISR refills the 64-word channel memory from `rd->sym` as it drains (`src/drv/dmx_rmt.h:7-9`). The CPU is free to service other outputs.

**RDM variant** (request TX on the same RMT channel, core 1): `rdmTx()` at `src/core/rdm_engine.cpp:96-110` calls `rmtDmxEncode(rd, pkt, len)` (line 103) then `rmt_transmit()` directly (line 105), waits with `rmt_tx_wait_all_done(rd->chan, 60)` (line 106, 60 ms timeout), toggles DE/RE GPIO (lines 102/107), and flushes the UART RX between encode and transmit (lines 101/109).

## 9. Protocol Layout

N/A (no wire protocol). This module encodes the DMX512 physical-layer frame structure but does not define a higher-layer protocol. The DMX512 frame layout it produces:

| Region | Duration | Level | Words |
|---|---|---|---|
| Break | `rd->breakTime` µs (default 176) | Low (normal) / High (inverted) | 1 symbol word (`src/drv/dmx_rmt.h:88-96`) |
| Mark-after-Break | `rd->mabTime` µs (default 12) | High (normal) / Low (inverted) | (same word) |
| Start code + 512 slots | 513 × 44 µs = 22572 µs | Encoded per `g_byteLut` | up to ~2822 words (`src/drv/dmx_rmt.h:27-29, 83-104`) |

The symbol word packing: `duration0`/`level0` (break or MAB), then per-byte run pairs from the LUT (`src/drv/dmx_rmt.h:69-75`).

## 10. Config Integration

This module itself reads **no** `Config` fields directly — it is a pure hardware driver. Configuration is applied by its consumers:

| Config field (on `DmxOutput`) | Consumer | CFG flags in `config_schema.cpp` | Applied at |
|---|---|---|---|
| `breakTime` | `output_init.cpp:79` | `OINT_L` → `CFG_LIVE` (line 172 of config_schema.cpp) | init + `updateOutputRuntime` (output_init.cpp:120) |
| `mabTime` | `output_init.cpp:80` | `OINT_L` → `CFG_LIVE` (line 173) | init + `updateOutputRuntime` (output_init.cpp:121) |
| `invert` | `output_init.cpp:81` | `OBOOL_L` → `CFG_LIVE` (line 174) | init + `updateOutputRuntime` (output_init.cpp:122) |
| `txPin` | `output_init.cpp:71` via `cfg.outputs[i].txPin` | `OINT` → `CFG_REBOOT` (line 161) | init only (`outputInitAll`) |
| `mode` | `output_init.cpp:64` → `resolveOutputMode()` | `OENUM_R` → `CFG_REBOOT` (line 171) | init only |
| `port`, `rxPin`, `rtsPin` | `output_init.cpp:67-78` | `CFG_REBOOT` (lines 160, 162, 163) | init only |
| `txRate` | `tasks.cpp:29` (`dmxPeriodMs`) | `OENUM` → `CFG_LIVE` (line 168) | live via `DMX_RATE_MS` table (tasks.cpp:25) |
| `txStyle` | `tasks.cpp:98` (`TXSTYLE_DELTA`) | `OENUM` → `CFG_LIVE` (line 169) | live |
| `inputMode` | `input_router.cpp:21-31` | `OENUM` → `CFG_LIVE` (line 175) | `inputRouterPoll` in loop |

## 11. Lifecycle

| Phase | Function | Call site | Notes |
|---|---|---|---|
| Pre-init | `dmxInitGuardBegin()` | `main.cpp:106` | NVS crash counter; may disable outputs before init. |
| Init | `outputInitAll()` → `rmtDmxInit()` | `output_init.cpp:35,71` | Per output: allocates `sym` buffer, creates RMT channel + copy encoder, enables, primes with a zero frame. |
| Runtime | `rmtDmxIdle()` → `rmtDmxKick()` → (background RMT) → `rmtDmxIdle()` | `tasks.cpp:102-103` | 1 ms tick on core 1. No `rmtDmxWait()` in the live tick path — non-blocking idle check only. |
| RDM runtime | `rmtDmxEncode()` + `rmt_transmit()` + `rmt_tx_wait_all_done()` | `rdm_engine.cpp:103-106` | Blocking wait (60 ms timeout) acceptable because RDM runs on the dedicated RDM task (priority 18). |
| Cleanup | (none) | — | No `rmtDmxDeinit()` exists (`src/drv/dmx_rmt.h:194-195`); the RMT channel is never released. Comment explains RDM now uses RMT-TX + RX-only UART so the channel is shared, not handed back. |

`updateOutputRuntime()` (`src/core/output_init.h:19`, `src/core/output_init.cpp:117-123`) pushes live config changes (break/MAB/invert) into the live `RmtDmx` without reboot.

## 12. Error Handling

| Return | Line | Behavior |
|---|---|---|
| `bool` from `rmtDmxInit` | 109 | Returns `false` on `heap_caps_malloc` failure (line 114-117, logs `[DMX] FAILED: RMT symbol buffer alloc in DRAM failed`), on combined DMA+non-DMA channel creation failure (lines 139-143, logs channel number + error), on copy-encoder failure (line 156), on `rmt_enable` failure (line 157), or implicitly falls through without explicit return on the classic-ESP32 path (line 153 returns `false`, line 158 falls through — **the ESP32 path has no `return true`**, so falls off the end of a `bool` function — undefined behavior, documented as a latent issue). |
| `void` from `rmtDmxKick` | 172 | No-op if `!rd->chan` (line 173). Otherwise always transmits; no error return from `rmt_transmit` is checked (`src/drv/dmx_rmt.h:172-179`). |
| `void` from `rmtDmxWait` | 181 | No-op if `!rd->chan` (line 182). Ignores the result of `rmt_tx_wait_all_done`. |
| `bool` from `rmtDmxIdle` | 190 | Returns `false` if `!rd->chan` (line 191); otherwise returns `rmt_tx_wait_all_done(rd->chan, 0) == ESP_OK`. |
| `int` from `rmtDmxEncode` | 83 | Returns `wi` (symbol count). If LUT not ready, builds it once (line 84). Breaks the slot loop early if `wi + n >= RMT_DMX_MAX_SYM` (line 102). |

Errors are logged via `Serial.println`/`Serial.printf` (lines 115, 140-141) — not `ESP_LOGE`. The 60-ms timeout in `rmt_tx_wait_all_done` for RDM (`src/core/rdm_engine.cpp:106`) bounds the blocking call.

## 13. Allocation

- **Symbol buffer** (`rd->sym`): allocated via `heap_caps_malloc(sizeof(rmt_symbol_word_t) * RMT_DMX_MAX_SYM, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)` at `src/drv/dmx_rmt.h:112-113`. `RMT_DMX_MAX_SYM = 3000` (line 29). Must be DRAM/DMA-capable for the RMT peripheral. The pointer is preserved across deinit/re-init (line 111 comment: "keep the buffer across a deinit/re-init").
- **LUT tables** (`g_byteLut`, etc.): `static` file-scope arrays at `src/drv/dmx_rmt.h:49-53`, totaling `2 × 256 × 6 × sizeof(rmt_symbol_word_t)` + 512 bytes ≈ ~12.3 KB of BSS (zero-init, not PSRAM).
- **Per-output `RmtDmx`**: embedded as `dmx_output_t.rmt` (`include/output.h:17`), stored in the global `g_outputs[MAX_OUTPUTS]` array at `src/core/output_init.cpp:14`. Not on PSRAM.
- **No task stack** allocated by this module — transmits from the caller's stack (`dmxTxTask` 8192-byte stack at `tasks.cpp:83`).

## 14. Timing

| Constraint | Value | Source | Basis |
|---|---|---|---|
| RMT clock resolution | 1 MHz | `RMT_DMX_RES_HZ` line 19 | 1 tick = 1 µs |
| DMX bit period | 4 µs | `RMT_DMX_BIT` line 20 | 250 kbaud |
| Default break | 176 µs | `RMT_DMX_BREAK_DEFAULT` line 22 | E1.11 minimum is 88 µs |
| Default MAB | 12 µs | `RMT_DMX_MAB_DEFAULT` line 23 | E1.11 minimum is 4 µs |
| DMX frame size | 513 slots | `DMX_PACKET_SIZE` line 26 | start code + 512 channels |
| Frame air time | ~24.3 ms | (513 × 44 µs + break + MAB) | 44 µs per slot (start + 8 data + 2 stop) |
| `dmxTxTask` tick | 1 ms | `tasks.cpp:142` (`vTaskDelayUntil`) | Hard real-time; priority 19 on core 1. |
| TX rate options | 25/24/30/40/50 ms | `DMX_RATE_MS` line 25 | Index into `txRate` config enum. |
| RDM TX wait timeout | 60 ms | `rdm_engine.cpp:106` | Bounds blocking `rmt_tx_wait_all_done`. |
| RMT idle check timeout | 0 ms (non-blocking) | `rmtDmxIdle` line 192 | `rmt_tx_wait_all_done(rd->chan, 0)`. |
| Priming frame absorb | 200 ms | `rmtDmxInit` line 165 | `rmt_tx_wait_all_done(rd->chan, 200)`. |

The RMT hardware clocks the symbol stream autonomously; once `rmt_transmit` is called the CPU is not on the timing path for individual bits (`src/drv/dmx_rmt.h:5-9`).

## 15. Traceability

| Claim | Source |
|---|---|
| RMT clocks DMX in hardware, CPU only fills a buffer | `src/drv/dmx_rmt.h:5-9` |
| RMT clock resolution is 1 MHz (1 tick = 1 µs) | `src/drv/dmx_rmt.h:19` |
| DMX bit is 4 µs (250 kbaud) | `src/drv/dmx_rmt.h:20` |
| Break default 176 µs, MAB default 12 µs | `src/drv/dmx_rmt.h:22-23` |
| DMX packet = 513 bytes (start code + 512 slots) | `src/drv/dmx_rmt.h:26`, `include/rdm_types.h:33` |
| Max symbol words = 3000 | `src/drv/dmx_rmt.h:29` |
| `RmtDmx` struct fields: chan, enc, sym, nsym, channel, hasDma, breakTime, mabTime, invert | `src/drv/dmx_rmt.h:31-41` |
| Symbol buffer allocated in DMA-capable DRAM | `src/drv/dmx_rmt.h:112-113` |
| LUT built lazily on first encode | `src/drv/dmx_rmt.h:84` |
| Byte LUT is run-length-encoded, even word count per byte | `src/drv/dmx_rmt.h:44-49` |
| Normal vs inverted LUT swaps levels | `src/drv/dmx_rmt.h:66-75` |
| `rmtDmxInit` creates RMT TX channel, selects DMA if available with fallback | `src/drv/dmx_rmt.h:109-167` |
| Only one DMA-capable RMT TX channel on ESP32-S3 | `src/drv/dmx_rmt.h:124-133` |
| Non-DMA fallback uses full channel memory block | `src/drv/dmx_rmt.h:137-138,148-153` |
| Priming zero-frame emitted to avoid first-frame flush timeout | `src/drv/dmx_rmt.h:158-165` |
| `rmtDmxKick` is async; RMT streams in background | `src/drv/dmx_rmt.h:169-179` |
| `rmtDmxIdle` is the non-blocking completion check | `src/drv/dmx_rmt.h:185-193` |
| `rmtDmxWait` is the blocking completion wait | `src/drv/dmx_rmt.h:181-184` |
| `rmtDmxSend` blocking convenience wrapper; no callers | `src/drv/dmx_rmt.h:197-199` |
| No `rmtDmxDeinit`; channel never released | `src/drv/dmx_rmt.h:194-195` |
| `dmxTxTask` pinned to core 1, priority 19, 8192 stack | `src/sys/tasks.cpp:83` |
| `netRxTask` on core 0, priority 5 | `src/sys/tasks.cpp:84` |
| Init via `rmtDmxInit` per output inside `outputInitAll` | `src/core/output_init.cpp:71` |
| Per-output break/mab/invert pushed from config | `src/core/output_init.cpp:79-81` |
| Live config update of break/mab/invert | `src/core/output_init.cpp:117-123` |
| TX loop: idle-check → snapshot → kick → stats | `src/sys/tasks.cpp:96-108` |
| `dmxFrameTick` 1 ms tick, delta-style skip logic | `src/sys/tasks.cpp:120-136` |
| Frame snapshot via seqlock | `src/core/dmx_buffer.cpp:7-17`, `src/core/dmx_buffer.h:31-40` |
| RDM TX uses `rmtDmxEncode` + `rmt_transmit` + `rmt_tx_wait_all_done(60)` | `src/core/rdm_engine.cpp:103-106` |
| DMX output mode enum (DMX-only vs RDM-full) | `include/output.h:11-14` |

## 16. Cross-References

| Module doc | Consumes | Provides to this module |
|---|---|---|
| [Output Init](./core-output-init.md) | `rmtDmxInit` at output_init.cpp:71 | — (this module is a leaf driver) |
| [DMX Buffer](./core-dmx-buffer.md) | `dmxBufSnapshot()` (tasks.cpp:100) feeds the frame this module transmits | — |
| [RDM Engine](./core-rdm-engine.md) | `rmtDmxEncode` (rdm_engine.cpp:103), direct `rmt_transmit` (rdm_engine.cpp:104) | RMT TX channel for RDM requests |
| [RDM Task](./core-rdm-task.md) | — | Runs on same core 1 as `dmxTxTask`, shares RMT channel |
| [Sys Tasks](./sys-tasks.md) | `rmtDmxIdle`/`rmtDmxKick` (tasks.cpp:102-103) | The 1 ms transmit tick |
| [Include Headers](./include-headers.md) | `dmx_output_t` struct (include/output.h:17) | `RmtDmx` type |

## 17. Limitations

- **Latent UB on classic ESP32 path**: `rmtDmxInit()` falls through without `return true` on the `#else` (non-S3) branch. The function is declared `bool`; reaching line 153 without a return reads an indeterminate value (`src/drv/dmx_rmt.h:148-153`). On S3 with `SOC_RMT_SUPPORT_DMA` this path is dead code; on classic ESP32 it is reachable.
- **`rmt_transmit` result is not checked** in `rmtDmxKick()` — a late-refill ISR failure or bus error is silently ignored (`src/drv/dmx_rmt.h:178`).
- **No runtime polarity validation**: `invert` swaps break/MAB levels (`src/drv/dmx_rmt.h:89-95`) but there is no assertion that inverted timing is electrically valid for the transceiver in use.
- **Single global LUT**: `g_byteLut`/`g_byteLutInv` are file-scope statics built once, shared by all `RmtDmx` instances. They are correct for any channel (byte encoding is channel-independent), but the lazy-init guard `g_lutReady` (line 53) is **not atomic** — if two cores ever called into this header concurrently before the LUT is built, a race exists. In practice only core 1 calls these functions (`src/drv/dmx_rmt.h:55`).
- **No per-channel error counter**: the symbol buffer overflow guard (`wi + n >= RMT_DMX_MAX_SYM`, line 102) breaks the slot loop silently, truncating the frame without notifying the caller or incrementing a stat.

## 18. Open Questions

- Not determinable from the inspected source code — whether `rmt_transmit` returning an error is expected to propagate to `stats().txFrames` or trigger a DE/RE re-init.
- Not determinable from the inspected source code — whether the 200 ms priming absorb (`src/drv/dmx_rmt.h:165`) is sufficient under worst-case WiFi coexistence on ESP32-S3.
- Not determinable from the inspected source code — the rationale for omitting a `rmtDmxDeinit()` (the comment at line 194-195 references a historical esp_dmx path; the current RDM design using a separate RX UART removes the need, but this is not asserted in code).
- Not determinable from the inspected source code — whether `RMT_DMX_MAX_SYM = 3000` (`src/drv/dmx_rmt.h:29`) is empirically validated against the worst-case 513-byte frame at maximum run lengths.

## 19. Testing

No test coverage for this domain. This is a hardware-bound RMT driver with no shim or host-test path. The native test suite (`test/native/`) covers `seqlock_test`, `merge_test`, `config_test`, and `rdm_types_test` — none exercise `dmx_rmt.h`. The `rdm_types_test` (`test/native/rdm_types_test.cpp:51`) only asserts `DMX_PACKET_SIZE == 513`, which this module also defines at `src/drv/dmx_rmt.h:26` (the test's canonical definition lives in `include/rdm_types.h:33`, so the two are duplicated).

## 20. History

- **Issue #64** (core separation, seqlock buffer): RMT replaced the UART+GPTimer DMX TX path to eliminate core-0 network-DMA-induced break corruption. This is the genesis comment at `src/drv/dmx_rmt.h:1-9`.
- **Issue #93** (multi-rate outputs): `rmtDmxIdle` non-blocking check was added so outputs at different TX rates never delay each other. Documented at `src/drv/dmx_rmt.h:185-189`.
- **ESP32-S3 DMA fallback**: the `#if defined(SOC_RMT_SUPPORT_DMA)` block with fallback to non-DMA ISR refill (`src/drv/dmx_rmt.h:124-154`) was added so the 2nd RMT channel does not fail to initialize on S3 (only channel 3 has DMA). Documented at lines 124-133.
- **RDM architecture (dispatch vs low-level)**: external callers dispatch via the RDM task; RDM TX on core 1 uses `rmtDmxEncode` + direct `rmt_transmit` (low-level primitives), matching the project decision that the RDM engine uses low-level RMT primitives directly (`project.md` decision `rdm_architecture_dispatch_vs_lowlevel`).
