# Module Specification: Shared Public Headers

## 1. Module Overview

The Shared Public Headers module constitutes a pure compile-time contract layer. It declares the type definitions, structural enums, field descriptors, and constants that every other layer of the system consumes. No runtime logic, no initialization, and no state machine exist within this module — it is a collection of declarations exclusively. The five logical header groups are: configuration schema (the persisted Config and per-output DmxOutput structs plus their descriptor tables), output runtime structs (the live dmx_output_t bound to hardware), the seqlock concurrency primitive, RDM type definitions (ANSI E1.20 types and structs), and Ethernet PHY mapping.

## 2. External Interfaces

The headers expose no callable functions to the scheduler. All runtime entry points reside in consuming modules. The only inline functions declared are pure compile-time helpers: an output-mode resolver that returns a mode enum based on a mode value and a pin number, the seqlock snapshot method, and the RMII PHY type mapper. These are called during initialization and runtime by the cfg, core, drv, and net layers.

**Exposed declarations:**
- `extern Config cfg` — the single live configuration singleton.
- `extern const CfgField CONFIG_FIELDS[]` and `extern const CfgOutputField OUTPUT_FIELDS[]` — schema descriptor tables defined in the config schema source.
- `extern int CONFIG_FIELD_COUNT` and `extern int OUTPUT_FIELD_COUNT` — counts of entries in the descriptor tables.
- `rmiiPhyType(int idx)` — maps a PHY index to an Ethernet PHY driver type.
- `resolveOutputMode(int modeVal, int rtsPin)` — resolves a runtime output mode from config and pin values.

## 3. State Machine

No state machine. These are static type and descriptor definitions only. The SeqLock struct encodes a lock-free single-writer / single-reader protocol, but this is a concurrency primitive, not a state machine — its sequence number is a counter, not a state.

## 4. Data Flow

The headers enable the following data flows that are executed by consuming modules:

1. **Config load** — The config engine reads NVS keys using CfgField and CfgOutputField descriptors, applies template values, and writes resolved values into the cfg singleton. Field offsets are computed at compile time via offsetof and stored in the descriptors.
2. **Output init** — The output initialization module reads cfg.outputs[] and calls resolveOutputMode() to construct dmx_output_t runtime instances, binding RMT channels, UART ports, and DE/RE GPIOs.
3. **Ethernet bring-up** — The Ethernet module calls rmiiPhyType() to select the correct PHY driver based on the configured wiredPhy index.
4. **DMX buffering** — The DMX buffer module uses the SeqLock struct to provide tearing-free snapshots of DMX frame data from core 0 (writer) to core 1 (transmit reader).
5. **RDM transactions** — The RDM engine uses the RDM type structs (UID, ACK, device info, sensor definitions) to parse and construct E1.20 frames via explicit field-by-field shift operations.

## 5. Configuration Integration

The headers define the schema that drives all configuration persistence, web UI generation, and serial console grammar.

**Config struct** — 47 global fields including hostname, protocol, LED settings, WiFi credentials, Ethernet configuration, RDM limits, and output array. The live instance is extern Config cfg.

**DmxOutput struct** — 24 per-output fields including universe/net/subnet, sACN settings, pin assignments, merge/loss modes, timing parameters, TX style, input mode, split mask, and loopback.

**CfgField descriptor** — One per Config field, containing: internal NVS key, JSON key, type kind (Int/Bool/Str/Enum), byte offset into Config, min/max range, human-readable label, UI group, config flags bitmask, and enum label array.

**CfgOutputField descriptor** — Same as CfgField but for DmxOutput fields, with an additional legacyKey0 field for output-0 NVS migration.

**Config flags bitmask:**
- CFG_SECRET — masks values in serial dumps (passwords).
- CFG_REBOOT — takes effect only after a reboot.
- CFG_READONLY — shown but not settable.
- CFG_NOWEB — hidden from the web config form.
- CFG_KEEPNE — blank web field is ignored, never blanks the value.
- CFG_LIVE — applies instantly on save, no reboot required.

**Field categorization by flag:**
- CFG_REBOOT fields: pins, GPIOs, UART port, LED type/pin, display pins, encoder/button pins, network mode, WiFi SSID.
- CFG_LIVE fields: universe/net/subnet/sACN, merge mode, loss mode, loss preset, failsafe timeout, TX rate, TX style, TX style source, break/MAB time, invert, input mode, split mask, loopback, brightness, protocol, RDM limits.

**Structural enums** (stored as int in config structs):
- Merge modes (Off, HTP, LTP, LTP-Takeover, Priority)
- Loss policies (Hold, Zero, Stop, Preset, Home)
- DMX input modes (Off, To-Network, Monitor)
- WiFi modes (Station, AP)
- Wired feedback policies (Retry, AP, Reboot, WiFi)
- RMII PHY families (LAN8720, IP101, RTL8201, DP83848, KSZ8081, JL1101)
- RMII clock modes (4 options)
- TX styles (Continuous, Delta)
- TX source markers (Local, ArtNet)

## 6. Lifecycle

1. **Compile time** — Templates are embedded into a generated header by the build system's template generator, which is invoked during every build via the extra scripts hook. The generated header is consumed by the config templates source.
2. **Boot** — NVS key migration runs first, then the config engine loads keys using the descriptor tables, applying template defaults as the neutral layer and NVS values as the overlay. Output sanitization resolves runtime instances from loaded config.
3. **Runtime** — Config mutations via web UI or serial console dispatch through the descriptor tables to read/write typed values with range clamping.

## 7. Error Handling

The headers declare no runtime behavior and thus no error handling. All error handling resides in consuming modules:
- Unknown config key returns an invalid-argument error.
- Template nesting beyond the compile-time cap returns an invalid-state error.
- Unknown template returns a not-found error.
- Unrecognized JSON/XML import keys return invalid-argument errors.
- Serial console unknown commands return an error string.
- All integer values are clamped to [min, max] declared in the descriptors.

## 8. Timing Constraints

No timing constraints on the headers themselves — they are compile-time declarations. The SeqLock primitive imposes lock-free semantics: the writer (core 0) is never delayed by the reader (core 1), and the snapshot method retries up to 8 times. If the writer wins all 8 retries, the caller transmits the previous frame rather than a torn one. The memory barrier is applied at every read and write of the sequence number.

## 9. Memory & Allocation Model

All data structures are static — no dynamic allocation within the headers module:
- Config cfg is a static global.
- CONFIG_FIELDS[] and OUTPUT_FIELDS[] are static const arrays in read-only data.
- dmx_output_t instances are static (allocated by the output init module).
- SeqLock.seq is a volatile uint32_t member.
- RDM type structs are packed and stack-allocated by consuming code; they are parsed field-by-field via explicit shift operations, never memcpy'd whole.
- RmtDmx symbol buffers are heap-allocated in DRAM by the RMT driver (not in the included headers).

On the native host test, Config cfg is a single shared global with no malloc for the config itself. The Arduino String class shim uses std::string internally.

## 10. Safety Considerations

- **Hardware limit awareness** — The ESP32-S3 has only 4 RMT TX channels. A preprocessor warning fires if MAX_OUTPUTS exceeds 4, but no runtime guard exists in the headers.
- **PHY mapping approximation** — The IP101 PHY index maps to the TLK110 driver, noted as pin-compatible but not guaranteed identical across all IP101 variants.
- **Default fallback** — The rmiiPhyType mapper falls through to the LAN8720 default for unknown indices.
- **RDM struct packing** — All RDM structs use the packed attribute with field-by-field parsing to avoid endianness and padding issues, but the packed attribute alone does not prevent unaligned access on all architectures.

## 11. Cross-Module Dependencies

- **Consumed by:** Config engine (cfg layer), output initialization (core layer), DMX buffer (core layer), RDM engine and RDM task (core layer), RMT driver (drv layer), Ethernet bring-up (net layer), LED status (sys layer), display (sys layer).
- **Depends on:** None — this module is the foundational type layer. It depends on no other project modules, only on the ESP-IDF ETH.h for the eth_phy_type_t enum and driver/uart.h for the uart_port_t enum.
- **External:** The config singleton, descriptor tables, and structs defined here are referenced by every other module in the system.

## 12. Testing Verification

- **Native config test** — tests template resolution, set/get round-trip, NVS save/load round-trip, and serial console grammar against the Config and DmxOutput structs and descriptor tables.
- **Unity config tests** — 8 tests covering template defaults, set/get, NVS round-trip, the LuxDMX-4uni template, and serial commands.
- **Unity RDM type tests** — tests UID pack/unpack, PID constants, and enum values for all RDM types.
- **Native seqlock test** — tests SeqLock.snapshot under concurrent write conditions.
- **No dedicated header self-test** — headers are validated only through consuming modules' tests.

## 13. Open Questions

- Whether CONFIG_LUXDMX_MAX_OUTPUTS can be set above 4 at the build-system level and what the runtime behavior would be. The preprocessor warning only fires at compile time; no runtime guard exists in the headers.
- Whether the volatile uint32_t seq field in dmx_output_t is read by any consumer other than the WebSocket frame-differencing path.
- Whether the rdm_pid_t typedef being a plain uint16_t (not an enum) should be revisited to enable compile-time exhaustiveness checking for manufacturer-specific PIDs.
- Whether additional error codes should be added to dmx_err_t, which currently only defines the success value.

## 14. History

The RMT-based DMX transmitter replaced the esp_dmx UART path to resolve issue #64 (broken breaks under network DMA contention). RDM types were extracted from the esp_dmx library into a standalone header to decouple the type definitions from the deprecated driver.

The dmx_output_t struct gained a port field for UART number selection, allowing RDM RX to target either UART1 or UART2 rather than relying on an implicit assignment.

The resolveOutputMode() inline helper was added to centralize the decision between DMX-only and RDM-full output modes based on the configured mode and RTS pin.

The seqlock primitive was introduced to replace lock-based buffer snapshots, eliminating core-to-core contention on the DMX transmit path.
