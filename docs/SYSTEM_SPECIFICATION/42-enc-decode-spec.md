# DMX Encode/Decode Specification

Domain: app.enc-decode

## 1. Module Overview

The Encode/Decode module is the gateway's DMX slot codec: it defines how DMX512 slot streams are interpreted entering the system and how channel values are framed when leaving it. A DMX512 frame is a start code byte followed by up to 512 data slots, each an 8-bit value in the range 0-255. The module owns the start-code classification that distinguishes a normal DMX data stream (start code `0x00`) from an RDM transport frame (start code `0xCC`), the slot-to-channel index mapping, the validation rules that keep decoded data within bounds, and the range checks that prevent channel references outside the 1-512 DMX address space.

Incoming packets from Art-Net, sACN, and Art-RDM are all normalized to this start-code + slot model before they reach the merge engine. Outgoing merged frames are assembled back into a start-code + slot buffer for the RMT transmit path. The module itself performs no network I/O and no hardware I/O; it is a pure data-shape contract applied by the protocol and driver layers on either side.

Owned: start-code classification, slot value range, slot-to-channel mapping, data validation, and channel range checking.
Delegated to: nothing.
Consumed by: the protocol layers (Art-Net, sACN, Art-RDM) on ingest, the merge engine on the normalized channel array, the frame router that selects this codec per output mode, and the RMT transmit path on egress.

## 2. External Interfaces

### Start-Code Classification

The leading byte of every received slot stream is a start code. It selects how the following slots are interpreted.

| Start code | Name | Interpretation |
|---|---|---|
| `0x00` | DMX | Normal DMX512 data. Slots 1-512 are intensity/channel values (0-255). |
| `0xCC` | RDM | Remote Device Management transport. The following bytes are an RDM command/response packet, not DMX channel values. |
| other | (reserved/other) | Not routed to the DMX channel path; handled by the protocol layer or dropped per the receiving protocol's rules. |

The start code itself occupies slot index 0 of the received frame and is never exposed as a DMX channel.

### Slot Value Domain

| Property | Value |
|---|---|
| Bit width | 8 bits (unsigned) |
| Value range | 0-255 inclusive |
| Channel granularity | One slot per channel |

Each data slot is an independent 8-bit unsigned value. There is no 16-bit or finer resolution in the base DMX model handled here.

### Slot-to-Channel Mapping

Within a 513-byte frame (1 start code + 512 data slots):

| Frame index | DMX channel | Role |
|---|---|---|
| 0 | — | Start code; not a channel |
| 1 | 1 | First channel (slot 0 in many network APIs) |
| 2 | 2 | Second channel |
| ... | ... | ... |
| 512 | 512 | Last channel |
| 513 | — | One past the end; never a valid channel |

The mapping from received slot index to DMX channel number is `channel = slot_index` for indices 1 through 512. Incoming frames with fewer than 512 slots leave the trailing channels at their prior value or zero, depending on the merge mode.

### Channel Address Space

| Boundary | Value |
|---|---|
| Minimum channel | 1 |
| Maximum channel | 512 |
| Zero channel | invalid; slot index 0 is the start code, never a channel |

Any reference to channel 0 or to a channel greater than 512 is invalid and out of range.

## 3. State Machine

There is no state machine internal to this module. The encode and decode operations are pure, stateless transforms over a single frame:

- **Decode**: read the start code, classify the frame, then copy slot bytes 1..N into the channel array with range validation.
- **Encode**: write the start code byte, then copy channel values 1..N into the slot stream in order.

Because the transform is stateless and per-frame, no transition logic is required. The per-output sender tracking, liveness, and merge timing that surround this codec live in upstream layers.

## 4. Data Flow

**Decode path (network in → channel array):**

1. **Frame reception**: A protocol layer receives a 513-byte frame (start code + up to 512 slots).
2. **Start-code dispatch**: The start code is read. A value of `0x00` designates a DMX data frame. A value of `0xCC` designates an RDM transport frame and is routed to the RDM subsystem rather than the channel array.
3. **Slot range validation**: The slot count is checked. Frames with more than 512 data slots are truncated or rejected; fewer than 512 slots are zero-filled for the missing trailing channels (subject to the merge mode's handling of missing data).
4. **Value validation**: Each data slot is already an 8-bit value (0-255) by the byte-level wire format; the decode step bounds-checks the slot index, not the byte value, since the byte value is constrained by the transport width.
5. **Channel mapping**: Each slot at index `i` (1..512) is mapped to channel `i` and written into the normalized channel array.
6. **Handoff**: The channel array is passed to the merge engine.

**Encode path (channel array → line):**

1. **Merge result**: The merge engine has produced a 513-byte buffer: start code at index 0, channel values at indices 1..512.
2. **Start-code write**: Index 0 is set to the appropriate start code (`0x00` for DMX transmission).
3. **Slot emission**: Channels 1..512 are emitted in index order as the slot stream.
4. **Range check**: No channel reference outside 1..512 is ever emitted; out-of-range references are rejected upstream before encode.

## 5. Configuration Integration

The codec itself takes no per-frame configuration. However, the system's handling of decoded frames is influenced by per-output configuration read by upstream layers:

| Aspect | Influences | Description |
|---|---|---|
| Output mode | output mode setting | Selects whether a port accepts DMX data (`0x00`) or participates in RDM transport (`0xCC`). |
| Merge mode | merge mode setting | Determines how frames with fewer than 512 slots are filled (zero, hold, or per-channel max). |
| Loss behaviour | loss mode setting | Selects what value the encoded frame carries when no live sender remains (zero / hold / preset / home / stop). |
| RDM mode | RDM enable setting | Gates whether `0xCC` frames are processed as RDM or skipped. |

The codec has no tunable parameters of its own; it applies the same start-code, value, and range rules to every frame.

## 6. Lifecycle

- **Decode**: Applied on every received frame, synchronously, within the network receive path. No setup or teardown.
- **Encode**: Applied on every transmitted DMX frame, synchronously, within the 1 ms DMX transmit tick. No setup or teardown.
- **Init**: No explicit init call; the module is a set of pure rules consulted by the protocol and driver layers.

## 7. Error Handling

| Condition | Behaviour |
|---|---|
| Start code other than `0x00` or `0xCC` | The frame is not routed to the DMX channel path. `0xCC` goes to RDM; other codes are handled or dropped per the receiving protocol's rules. |
| Slot index 0 referenced as a channel | Rejected: index 0 is the start code, not a channel. Channel numbers start at 1. |
| Channel reference below 1 | Rejected as out of range (below minimum). |
| Channel reference above 512 | Rejected as out of range (above maximum). |
| Frame with more than 512 data slots | Truncated to 512 or rejected, depending on the protocol layer; the codec enforces the 512-slot ceiling. |
| Frame with fewer than 512 data slots | Accepted; trailing channels adopt the merge mode's behaviour (zero-fill, hold, or per-channel contribution). |
| Slot byte value out of 0-255 | Impossible at the byte level; a data slot is an 8-bit byte by definition, so values are always in range. Validation is structural, not value-range. |

Validation is structural (counts and indices). Because each slot is a single byte on the wire, value out-of-range cannot occur without a transport-level corruption that is caught by higher-layer checksums.

## 8. Timing Constraints

| Item | Value |
|---|---|
| Decode latency | Sub-microsecond per frame (a byte copy with index bounds) |
| Encode latency | Sub-microsecond per frame |
| Frame rate | Bounded by the 44 Hz DMX transmit ceiling (25 ms minimum inter-frame) and the 1 ms transmit tick |
| No per-slot timing | Slot decoding/encoding does not touch the 4 µs-per-slot bit timing; that is owned by the RMT transmit path |

The codec imposes no timing on the physical layer. The 4 µs slot bit time and the break/mark-after-break cadence are the responsibility of the RMT driver, not this module.

## 9. Memory and Allocation Model

- The decode and encode transforms operate in place over caller-provided buffers. No heap allocation occurs within the codec.
- The channel array is a fixed 513-byte buffer per output (1 start code + 512 slots), owned by the merge engine and DMX buffer layer respectively.
- No dynamic structures, tables, or caches are retained by the codec between frames.

## 10. Safety Considerations

- **Start-code isolation**: Misrouting an RDM frame (`0xCC`) as DMX channel data would corrupt lighting; the codec classifies on the start code before any channel array is written, so `0xCC` frames never reach the DMX channel path.
- **Index bounds**: Channel references are range-checked against 1..512 before any write. A malformed frame claiming 600 slots is truncated, never causing an out-of-bounds write.
- **Zero channel**: Index 0 is reserved for the start code and is never exposed as a channel, preventing off-by-one confusion between "the first byte" and "channel 1".
- **Statelessness**: Because the transform carries no inter-frame state, a corrupted or short frame cannot poison subsequent frames; each frame is independently classified and validated.
- **No real-time impact**: The codec's per-frame work is negligible relative to the 1 ms tick, so even under worst-case load the physical DMX timing is unaffected.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| Art-Net protocol | upstream (producer/consumer) | Produces `0x00` DMX frames and `0xCC` RDM frames on decode; consumes encoded `0x00` frames on transmit. |
| sACN protocol | upstream (producer) | Produces normalized DMX data for decode. |
| RDM engine | upstream (consumer) | Receives `0xCC` frames routed from the start-code dispatcher; relies on the same slot framing for RDM transport. |
| Frame router | upstream (consumer) | Selects the active output mode (DMX vs RDM) that determines which start codes are accepted per port. |
| Merge engine | downstream (consumer) | Receives the decoded channel array; owns the per-output 513-byte frame buffer. |
| DMX buffer | downstream (consumer/provner) | Holds the encoded 513-byte frame for the transmit tick. |
| RMT transmit | downstream (consumer) | Transmits the encoded slot stream at 250 kbaud. |

## 12. Testing Verification

The codec's pure functions (start-code classification, slot-to-channel mapping, index range checking, truncation of over-long frames) are structural and would be straightforward to unit-test with canned 513-byte frames and assertions on the resulting channel array. No host-native test currently exercises the codec path: the existing native suite (`config_test`, `seqlock_test`, `merge_test`, `rdm_types_test`) covers configuration resolution, the seqlock primitive, the merge engine, and RDM type constants, but does not include a dedicated encode/decode test harness. The value range itself (0-255) is guaranteed by the byte-level transport and is not independently tested. Adding a host test that feeds frames with edge-case start codes, under-length payloads, and over-length payloads would lock in the truncation and rejection rules.

## 13. Open Questions

1. Whether over-length frames should be truncated or rejected outright by the codec, or whether that policy is left to each protocol layer.
2. Whether under-length frames should zero-fill trailing channels at the codec, or whether that is solely the merge mode's responsibility.
3. Whether non-`0x00`/non-`0xCC` start codes should be surfaced to the codec at all, or dropped entirely before reaching this module's classification step.
4. Whether 16-bit DMX-PST or other extended slot encodings are expected to flow through this module or are handled by a separate extended-range path.
5. Whether the codec will gain any configuration of its own (for example a per-output strict-length policy), or remains a pure structural rule set.

## 14. History

The encode/decode contract was separated from the protocol and driver layers during the five-layer architecture refactor so that every ingress path (Art-Net, sACN, Art-RDM) and the egress RMT path share one definition of the DMX slot frame: start code + 512 channels, 0-255 each. Centralising the start-code classification (`0x00` DMX vs `0xCC` RDM) here prevents any protocol layer from accidentally writing RDM bytes into the DMX channel array. The 513-byte frame shape (one start code + 512 slots) mirrors the per-output DMX buffer layout used by the transmit tick, keeping decode and encode symmetric. The channel address space was fixed to 1-512 (with index 0 reserved for the start code) to match the physical DMX512-A address space and to make off-by-one errors structurally impossible.
