# Shared Public Headers — Technical Reference

Domain: include (cross-cutting across drv, cfg, core, net, app/sys)

## 1. Domain Scope

The `include/` directory declares the shared type definitions, configuration descriptors, and constants that every layer of the 5-layer model consumes. No runtime logic lives here — these are pure compile-time declarations.

**Config schema** (`config_schema.h`, `config_types.h`, `config_enums.h`): the persisted `Config` struct and `DmxOutput` struct, the field-descriptor tables (`CfgField` / `CfgOutputField`) that drive NVS serialization, the web config form, and the serial console grammar, and the structural enums (merge modes, loss policies, PHY families) referenced as `int` indices inside `Config`.

**Runtime output** (`output.h`): the live `dmx_output_t` struct that binds the RMT TX channel, UART RX port, and DE/RE GPIO into one instance the DMX task owns; not persisted.

**Concurrency primitive** (`seqlock.h`): a single-writer/single-reader seqlock for tearing-free cross-core buffer snapshots.

**RDM types** (`rdm_types.h`): ANSI E1.20 type constants, UID, command classes, PIDs, response type, and parameter-response structs used by the RDM engine and its UART RX driver.

**Ethernet PHY** (`eth_phy.h`): the RMII PHY name table and the `eth_phy_type_t` mapping consumed by the Ethernet bring-up code.

This module owns no behavior — it delegates all runtime work to the `cfg`, `core`, `drv`, and `net` layers that include these headers. It is consumed by every other module.

## 2. Layer Mapping

| Header | Layer(s) consuming |
|---|---|
| config_schema.h | cfg, core, net, sys, app |
| config_types.h | cfg |
| config_enums.h | cfg, core, net |
| output.h | drv, core |
| seqlock.h | core (dmx_buffer) |
| rdm_types.h | core (rdm_engine, rdm_disc, rdm_task, rdm_typed) |
| eth_phy.h | net (ethernet) |

The headers sit at the interface between layers: `cfg` declares the schema, `core` instantiates `dmx_output_t`, `net` uses `eth_phy.h` for PHY selection.

## 3. Source Files

| File | Role |
|---|---|
| `include/config_schema.h` | Defines `DmxOutput` (24 per-port fields), `Config` (47 global fields), `MAX_OUTPUTS` constexpr, and `extern Config cfg` (the single live config instance) |
| `include/config_types.h` | Defines `CfgKind`, `CfgFlags`, `CfgField` (descriptor for `Config` fields), `CfgOutputField` (descriptor for `DmxOutput` fields), and extern table pointers `CONFIG_FIELDS[]` / `OUTPUT_FIELDS[]` |
| `include/config_enums.h` | Structural enums: merge modes, loss policies, DMX input modes, WiFi modes, wired-feedback policies, RMII PHY families, RMII clock modes, TX styles, TX-source markers |
| `include/output.h` | Defines `output_mode_t` enum and `dmx_output_t` live runtime struct (RMT channel + UART + DE/RE state); provides `resolveOutputMode()` inline helper |
| `include/seqlock.h` | Defines `SeqLock` struct with `writeBegin` / `writeEnd` / `snapshot` — single-writer / single-reader lock-free consistency guard |
| `include/rdm_types.h` | Defines `DMX_PACKET_SIZE`, `RDM_ASCII_SIZE_MAX`, `rdm_uid_t`, `rdm_cc_t`, `rdm_pid_t` + PID constants, `rdm_response_type_t`, `rdm_nr_t`, `dmx_err_t`, `rdm_ack_t`, `rdm_device_info_t`, `rdm_sensor_definition_t`, `rdm_sensor_value_t`, `rdm_sensor_type_t`, `rdm_units_t` |
| `include/eth_phy.h` | Defines `RMII_PHY_NAMES` string table and `rmiiPhyType()` inline enum-to-`eth_phy_type_t` mapper (guarded by `HAS_ETH_RMII`) |

## 4. Data Structures

### `DmxOutput` — `include/config_schema.h:11`

Per-DMx-output persisted settings (24 fields). Clamped to `MAX_OUTPUTS` = `CONFIG_LUXDMX_MAX_OUTPUTS` (`config_schema.h:7`), which defaults to 4 (`platformio.ini:51` sets `-DCONFIG_LUXDMX_MAX_OUTPUTS=4`).

| Field | Type | Line | Description |
|---|---|---|---|
| `enabled` | `bool` | `config_schema.h:12` | Output enable flag |
| `universe` | `int` | `config_schema.h:13` | Art-Net universe (0–15); 15-bit with net/subnet (0–32767) |
| `net` | `int` | `config_schema.h:14` | Art-Net net switch (0–127) |
| `subnet` | `int` | `config_schema.h:15` | Art-Net subnet (0–15) |
| `sacnUniverse` | `int` | `config_schema.h:16` | sACN streaming universe (0 = auto, derive from universe+1) |
| `sacnSync` | `int` | `config_schema.h:17` | sACN sync universe (0 = none) |
| `port` | `int` | `config_schema.h:18` | UART number for RDM RX (1=UART1, 2=UART2); ignored for DMX-only outputs |
| `txPin` | `int` | `config_schema.h:19` | DMX TX GPIO (RMT output) |
| `rxPin` | `int` | `config_schema.h:20` | DMX RX GPIO (-1 = output only, no RDM) |
| `rtsPin` | `int` | `config_schema.h:21` | RTS / DE-RE GPIO (-1 = auto-direction module / no RDM) |
| `mergeMode` | `int` | `config_schema.h:22` | How to combine multiple sources (see `MERGE_*` enums) |
| `lossMode` | `int` | `config_schema.h:23` | What to send when all sources go silent (see `LOSS_*` enums) |
| `lossPreset` | `int` | `config_schema.h:24` | Scene preset index recalled on signal loss (when `lossMode=LOSS_PRESET`) |
| `failsafeTimeout` | `int` | `config_schema.h:25` | Per-port source-loss timeout in seconds (0 = use global default) |
| `txRate` | `int` | `config_schema.h:26` | Index into `DMX_RATE_MS` — free-running period for this port |
| `txStyle` | `int` | `config_schema.h:27` | 0 = continuous (free-run at txRate), 1 = delta (one frame per input packet) |
| `txStyleSrc` | `int` | `config_schema.h:28` | 0 = set locally, 1 = set by controller via Art-Net |
| `mode` | `int` | `config_schema.h:29` | Output mode (see `output_mode_t`: 0 = DMX only, 1 = RDM full) |
| `breakTime` | `int` | `config_schema.h:30` | DMX break time in µs (spec: 88–100000, default 176) |
| `mabTime` | `int` | `config_schema.h:31` | DMX mark-after-break in µs (spec: 0–100000, default 12) |
| `invert` | `int` | `config_schema.h:32` | DMX polarity inversion (0 = normal, 1 = inverted) |
| `inputMode` | `int` | `config_schema.h:33` | DMX input mode: 0 = off, 1 = retransmit to network, 2 = monitor/loopback |
| `splitMask` | `int` | `config_schema.h:34` | Bitmask of additional output indices receiving the same universe |
| `loopback` | `int` | `config_schema.h:35` | Virtual universe that also receives this output's frame (0 = none) |

### `Config` — `include/config_schema.h:38`

Global persisted configuration (47 fields). The single live instance is `extern Config cfg` (`config_schema.h:91`), defined in `src/cfg/config_core.cpp:12`.

Key fields consumed outside the cfg layer:

| Field | Type | Line | Consumed by |
|---|---|---|---|
| `hostname` | `String` | `config_schema.h:39` | main.cpp:79 (mDNS), output_init |
| `protocol` | `int` | `config_schema.h:42` | main.cpp:85–91 (MDNS service registration), main.cpp:118 |
| `ledPin` | `int` | `config_schema.h:43` | sys/led_status |
| `ledType` | `int` | `config_schema.h:44` | sys/led_status |
| `ledR..ledW` | `int` | `config_schema.h:45` | sys/led_status (5-LED panel) |
| `ledBrR..ledBrW` | `int` | `config_schema.h:46` | sys/led_status (PWM brightness) |
| `outputs[MAX_OUTPUTS]` | `DmxOutput` | `config_schema.h:47` | core/output_init, net/artnet, core/merge_engine |
| `dispType` / `dispSda` / `dispScl` | `int` | `config_schema.h:48–49` | sys/display |
| `ethW5500` | `bool` | `config_schema.h:58` | main.cpp:61, net/ethernet |
| `ethSpiPhy` | `int` | `config_schema.h:59` | net/ethernet |
| `wiredPhy` | `int` | `config_schema.h:60` | main.cpp:61, net/ethernet |
| `useEthernet` | `bool` | `config_schema.h:62` | main.cpp:59 |
| `wifiMode` | `int` | `config_schema.h:63` | main.cpp:71 |
| `wifiSsid` / `wifiPsk` | `String` | `config_schema.h:64–65` | net/net_state |
| `linkLossMode` | `int` | `config_schema.h:67` | main.cpp:66, main.cpp:210 |
| `apFallback` | `bool` | `config_schema.h:66` | derived from `linkLossMode` at config_core.cpp:210 |
| `autoIpFallback` | `bool` | `config_schema.h:72` | net/net_state |
| `artnetRdm` | `bool` | `config_schema.h:87` | net/artnet (RDM over Art-Net enable) |
| `rdmMaxDev` | `int` | `config_schema.h:88` | core/rdm_engine (discovery device limit) |

### `CfgKind` — `include/config_types.h:13`

```
enum class CfgKind : uint8_t { Int, Bool, Str, Enum };
```
Four field types the config engine dispatches on during load/save/dump/import.

### `CfgFlags` — `include/config_types.h:15`

```
enum CfgFlags : uint16_t {
    CFG_NONE     = 0,       // config_types.h:16
    CFG_SECRET   = 1 << 0,  // mask value in serial dumps (passwords) — config_types.h:17
    CFG_REBOOT   = 1 << 1,  // takes effect only after a reboot — config_types.h:18
    CFG_READONLY = 1 << 2,  // shown but not settable — config_types.h:19
    CFG_NOWEB    = 1 << 3,  // not part of the /config form — config_types.h:20
    CFG_KEEPNE   = 1 << 4,  // blank web field is ignored, never blanks — config_types.h:21
    CFG_LIVE     = 1 << 5,  // applies instantly on save, no reboot — config_types.h:22
};
```

### `CfgField` — `include/config_types.h:25`

Schema descriptor for a single `Config` (global) field.

| Field | Type | Line | Description |
|---|---|---|---|
| `key` | `const char*` | `config_types.h:26` | Internal key name (NVS namespace key) |
| `jsonKey` | `const char*` | `config_types.h:27` | JSON key for export/import |
| `kind` | `CfgKind` | `config_types.h:28` | Field type discriminator |
| `offset` | `uint16_t` | `config_types.h:29` | `offsetof(Config, member)` — pointer arithmetic to the live value |
| `min`, `max` | `int32_t` | `config_types.h:30` | Inclusive range (clamped by `writeTyped` at config_core.cpp:47) |
| `label` | `const char*` | `config_types.h:31` | Human-readable label for web/serial UI |
| `group` | `const char*` | `config_types.h:32` | UI grouping (e.g. "Network", "Status LED") |
| `flags` | `uint16_t` | `config_types.h:33` | Bitmask of `CfgFlags` |
| `enumLabels` | `const char* const*` | `config_types.h:34` | NULL for non-enum; array of display strings |
| `enumCount` | `uint8_t` | `config_types.h:35` | Length of `enumLabels` |

### `CfgOutputField` — `include/config_types.h:38`

Same as `CfgField` but for per-output fields: `offset` is `offsetof(DmxOutput, member)`, and a `legacyKey0` field (`config_types.h:43`) provides a fallback NVS key for output 0 legacy migration.

### `CONFIG_FIELDS[]` / `OUTPUT_FIELDS[]` — `include/config_types.h:51–54`

Extern pointer + count pairs defined in `src/cfg/config_schema.cpp:46` / `config_schema.cpp:153`. The engine iterates these to serialize, validate, and display every setting. `CONFIG_FIELD_COUNT` (`config_schema.cpp:133`) and `OUTPUT_FIELD_COUNT` (`config_schema.cpp:179`) are `ARZS`-derived (`config_schema.cpp:9`).

### Structural enums — `include/config_enums.h`

| Enum | Line | Values |
|---|---|---|
| `MERGE_OFF/HTP/LTP/LTP_TAKEOVER/PRIORITY` | `config_enums.h:5` | 0–4 |
| `LOSS_HOLD/ZERO/STOP/PRESET/HOME` | `config_enums.h:6` | 0–4 |
| `DMX_IN_OFF/TO_NET/MONITOR` | `config_enums.h:7` | 0–2 |
| `NET_WIFI_STA/AP` | `config_enums.h:8` | 0–1 |
| `WIRED_FB_RETRY/AP/REBOOT/WIFI` | `config_enums.h:9` | 0–3 |
| `RMII_PHY_LAN8720..JL1101` | `config_enums.h:16` | 0–5 (count: `RMII_PHY_COUNT` at `config_enums.h:12`) |
| `RMII_CLK_GPIO0_IN..GPIO17_OUT` | `config_enums.h:20` | 0–3 |
| `TXSTYLE_CONTINUOUS/DELTA` | `config_enums.h:24` | 0–1 |
| `TXSRC_LOCAL/ARTNET` | `config_enums.h:27` | 0–1 |

### `output_mode_t` — `include/output.h:11`

```
enum output_mode_t : uint8_t {
    OUTPUT_MODE_DMX_ONLY = 0,  // RMT TX only, auto-direction RS485, no RDM — output.h:12
    OUTPUT_MODE_RDM_FULL = 1,  // RMT TX + UART RX + DE/RE GPIO, RDM E1.20 capable — output.h:13
};
```

### `dmx_output_t` — `include/output.h:16`

Live (non-persisted) runtime state for one DMX output. Owned by the DMX task on core 1.

| Field | Type | Line | Description |
|---|---|---|---|
| `rmt` | `RmtDmx` | `output.h:17` | RMT TX channel handle, symbol buffer, encoder |
| `index` | `int` | `output.h:18` | Output index (0–3) |
| `rmtChannel` | `int` | `output.h:19` | RMT TX channel number (0–3) |
| `mode` | `output_mode_t` | `output.h:20` | DMX-only or RDM-full |
| `dePin` | `int` | `output.h:21` | DE/RE GPIO (-1 = auto-direction) |
| `rxPin` | `int` | `output.h:22` | UART RX pin |
| `uartPort` | `uart_port_t` | `output.h:23` | ESP-IDF UART port (from `driver/uart.h`) |
| `ready` | `bool` | `output.h:24` | True once init succeeded |
| `seq` | `volatile uint32_t` | `output.h:25` | Monotonic frame counter for change detection |

### `resolveOutputMode()` — `include/output.h:28`

Inline helper: returns `OUTPUT_MODE_RDM_FULL` if `modeVal == 1` OR `rtsPin >= 0`; otherwise `OUTPUT_MODE_DMX_ONLY`. Called by `output_init.cpp` during output initialization.

### `SeqLock` — `include/seqlock.h:11`

```
struct SeqLock {
    volatile uint32_t seq = 0;           // seqlock.h:12
    void writeBegin();                    // seqlock.h:14 — bumps seq (odd = mid-write)
    void writeEnd();                      // seqlock.h:15
    bool snapshot(src, dst, n) const;     // seqlock.h:21 — retries up to 8 times
};
```

The writer increments `seq` to an odd value at `writeBegin`, then to an even value at `writeEnd` (`seqlock.h:14`–`15`). The reader reads `seq`, takes a `memcpy`, then re-reads `seq`; if the value changed or was odd, it retries up to 8 times (`seqlock.h:22–30`). On failure (writer keeps winning), the caller holds the previous frame rather than transmitting a torn one (`seqlock.h:19`–`20`). Uses `__sync_synchronize()` as the memory barrier (`seqlock.h:14`, `seqlock.h:15`, `seqlock.h:25`, `seqlock.h:27`).

### RDM types — `include/rdm_types.h`

**Constants:**
- `DMX_PACKET_SIZE` (513) — `rdm_types.h:33` — start code + 512 slots
- `RDM_ASCII_SIZE_MAX` (33) — `rdm_types.h:37` — E1.20 ASCII cap + NUL terminator

**`rdm_uid_t`** — `rdm_types.h:43`:
| Field | Type | Line |
|---|---|---|
| `man_id` | `uint16_t` | `rdm_types.h:44` |
| `dev_id` | `uint32_t` | `rdm_types.h:45` |

Constants: `RDM_UID_BROADCAST_ALL` (`rdm_types.h:50`), `RDM_UID_MAX` (`rdm_types.h:54`). Helper: `rdm_uid_is_eq()` (`rdm_types.h:56`).

**`rdm_cc_t`** — `rdm_types.h:71`: `DISC_COMMAND` (0x10), `DISC_COMMAND_RESPONSE` (0x11), `GET_COMMAND` (0x20), `GET_COMMAND_RESPONSE` (0x21), `SET_COMMAND` (0x30), `SET_COMMAND_RESPONSE` (0x31).

**`rdm_pid_t`** — `rdm_types.h:85`: typedef for `uint16_t`.

**PID constants** — `rdm_types.h:87–111`: `DISC_UNIQUE_BRANCH` (0x0001), `DISC_MUTE` (0x0002), `DISC_UN_MUTE` (0x0003), `STATUS_MESSAGE` (0x0030), `DEVICE_INFO` (0x0060), `DEVICE_MODEL_DESCRIPTION` (0x0080), `MANUFACTURER_LABEL` (0x0081), `DEVICE_LABEL` (0x0082), `SOFTWARE_VERSION_LABEL` (0x00c0), `DMX_PERSONALITY` (0x00e0), `DMX_PERSONALITY_DESCRIPTION` (0x00e1), `DMX_START_ADDRESS` (0x00f0), `SENSOR_DEFINITION` (0x0200), `SENSOR_VALUE` (0x0201), `SENSOR_RECORD` (0x0202), `IDENTIFY_MODE` (0x1011), `DEVICE_HOURS` (0x1010), `DEVICE_POWER` (0x1012), `BURN_IN` (0x1013), `IDENTIFY_DEVICE` (0x1000), `DEVICE_MODE` (0x1101), `DEVICE_MODES` (0x1100), `DEVICE_MODE_DESCRIPTION` (0x1102).

**`rdm_response_type_t`** — `rdm_types.h:116`: `ACK` (0x00), `ACK_TIMER` (0x01), `NACK_REASON` (0x02), `ACK_OVERFLOW` (0x03), `INVALID` (0xfe — firmware-internal), `NONE` (0xff — firmware-internal).

**`rdm_nr_t`** — `rdm_types.h:126`: typedef for `uint16_t` (NACK reason).

**`dmx_err_t`** — `rdm_types.h:131`: enum with only `DMX_OK` (0x00) — comment at `rdm_types.h:128` notes this is "always DMX_OK" in practice.

**`rdm_ack_t`** — `rdm_types.h:136`:
| Field | Type | Line | Description |
|---|---|---|---|
| `err` | `dmx_err_t` | `rdm_types.h:138` | Non-zero if read failed |
| `size` | `size_t` | `rdm_types.h:140` | Response packet size in bytes |
| `src_uid` | `rdm_uid_t` | `rdm_types.h:142` | UID of responding device |
| `pid` | `rdm_pid_t` | `rdm_types.h:144` | Echo of request PID |
| `type` | `rdm_response_type_t` | `rdm_types.h:147` | ACK/NACK/none |
| `message_count` | `int` | `rdm_types.h:150` | Queued-message count from responder |
| union | — | `rdm_types.h:151` | `pdl` (ACK), `timer` (ACK_TIMER), `nack_reason` (NACK) |

The structs `rdm_device_info_t` (`rdm_types.h:174`), `rdm_sensor_definition_t` (`rdm_types.h:190`), `rdm_sensor_value_t` (`rdm_types.h:216`), `rdm_dmx_personality_t` (`rdm_types.h:166`), `rdm_sensor_type_t` (`rdm_types.h:226`), `rdm_units_t` (`rdm_types.h:239`) are all `__attribute__((packed))` with field-by-field parsing noted at `rdm_types.h:13–21` (never `memcpy`'d whole).

### `RMII_PHY_NAMES` / `rmiiPhyType()` — `include/eth_phy.h`

`RMII_PHY_NAMES[]` (`eth_phy.h:7`): 6-entry string table `{"LAN8720", "IP101", "RTL8201", "DP83848", "KSZ8081", "JL1101"}` indexed by the `RMII_PHY_*` enum (`config_enums.h:16`). `rmiiPhyType(int idx)` (`eth_phy.h:12`) maps the index to `eth_phy_type_t` (`ETH.h`); note IP101 maps to `ETH_PHY_TLK110` (`eth_phy.h:15`) — pin-compatible. Guarded by `HAS_ETH_RMII` (`eth_phy.h:6`).

## 5. Concurrency

These are compile-time declarations only — no runtime concurrency. The `SeqLock` struct (`seqlock.h:11`) is an **exception**: it encodes a single-writer / single-reader lock-free protocol. The writer runs on core 0 (`netRxTask` / `dmx_buffer.cpp`) and the reader on core 1 (`dmxTxTask` / `dmx_buffer.cpp`); see [core-dmx-buffer](./core-dmx-buffer.md) for the instantiation. `dmx_output_t.seq` (`output.h:25`) is declared `volatile` because it is incremented by core 1 (DMX task) and read by core 0 (WebSocket) without a lock — the 32-bit increment is atomic on the ESP32.

## 6. State Machine

No state machine. These are static type definitions only.

## 7. Entry Points

N/A — these headers declare no functions callable by the scheduler. All entry points are in the consuming modules (e.g. `cfgcore::load()` in `src/cfg/config_core.cpp:167`, called from `main.cpp:46`).

## 8. Data Flow

N/A — these are declarations. The flow they enable is:

1. `main.cpp:46` calls `cfgcore::load()` → reads NVS keys via `Preferences` → writes fields into the `cfg` singleton (`config_core.cpp:12`) via `CfgField`/`CfgOutputField` descriptor tables → `config_schema.cpp:46/153`
2. `main.cpp:47` calls `sanitizeOutputs()` (`output_init.cpp:8`) → resolves `dmx_output_t` runtime instances from `cfg.outputs[]` using `resolveOutputMode()` (`output.h:28`)
3. `main.cpp:106–107` calls `dmxInitGuardBegin()` / `outputInitAll()` → instantiates `RmtDmx` inside `dmx_output_t.rmt` (`output.h:17`)
4. `netRxTask` (core 0) writes DMX frames into `dmxBuffers[i].data` behind `SeqLock` (`seqlock.h:11`) → `dmxTxTask` (core 1) calls `SeqLock::snapshot()` (`seqlock.h:21`) to copy → transmits via `RmtDmx` (`dmx_rmt.h:31`)

## 9. Protocol Layout

**Template INI format** (`templates/*.ini`, parsed by `cfgcore::applyTemplateText` at `config_core.cpp:120`):

```
key=value          # one per line, stripped of whitespace (config_core.cpp:131-138)
extends=_base      # inherits another template (config_core.cpp:140)
# comment          # ignored (config_core.cpp:134)
```

**NVS key naming** (`cfgcore::load()` at `config_core.cpp:167`):
- Global fields: `f.key` (the `CfgField::key` string, e.g. `"hostname"`, `"protocol"`)
- Output 0–3 fields: `a_<suffix>`, `b_<suffix>`, `c_<suffix>`, `d_<suffix>` (`config_core.cpp:24`, `config_core.cpp:180`)
- Legacy output 0–1 fallback: `o0_<suffix>`, `o1_<suffix>` (`config_core.cpp:25`, migrated at `config_core.cpp:182–196`)

**RDM wire layout** — not wire-critical; `rdm_types.h:13` states these structs are filled field-by-field by `rdm_rmt.h` with explicit shifts, never `memcpy`'d whole. Packed attributes retained for standard compliance (`rdm_types.h:17`).

## 10. Config Integration

All `Config` and `DmxOutput` fields are described by the schema tables in `config_schema.cpp:46–133` (global) and `config_schema.cpp:153–178` (per-output). The `flags` bitmask on each `CfgField` / `CfgOutputField` determines `CFG_LIVE` vs `CFG_REBOOT`:

- **`CFG_REBOOT`** fields (take effect only after reboot): pins, GPIOs, UART port, LED type/pin, display pins, encoder/button pins, network mode, WiFi SSID (most of `config_schema.cpp` fields use `IFIELD`/`BFIELD`/`EFIELD` macros which OR in `CFG_REBOOT` at `config_schema.cpp:30–36`).
- **`CFG_LIVE`** fields (apply instantly): universe/net/subnet/sacn, merge mode, loss mode, loss preset, failsafe timeout, TX rate, TX style, TX style source, break/MAB time, invert, input mode, split mask, loopback, brightness, protocol, board, ipProg, autoIp, dscp, timecode, syslog, webhook, artnetRdm (`IFIELD_L`/`BFIELD_L`/`EFIELD_L` macros at `config_schema.cpp:38–44`; output live fields use `_L` macros at `config_schema.cpp:137–138`).

`cfg.hostname` is read directly at `main.cpp:79` for mDNS; `cfg.protocol` gates MDNS service registration (`main.cpp:85–91`).

## 11. Lifecycle

1. **Compile time** — `tools/gen_config_templates.py` (`extra_scripts.py:132`) embeds `templates/*.ini` into `src/generated/config_templates.gen.h`, pulled into the build by `src/config_templates_gen.cpp:3`.
2. **Boot** — `main.cpp:45` calls `nvs_migrate::migrateNvsKeys("dmxgw")` → `main.cpp:46` calls `cfgcore::load()` → `main.cpp:47` calls `sanitizeOutputs()`.
3. **Runtime** — `main.cpp:135` calls `cfgserial::poll()` each loop iteration; config mutations via web UI trigger `cfgcore::setValue` → `saveConfig()` (`main.cpp:142`, `artnet_bridge.cpp:103`, `net_state.cpp:101`, `web_routes.cpp:276`).

## 12. Error Handling

- `cfgcore::setValue` returns `ESP_ERR_INVALID_ARG` on unknown key (`config_core.cpp:90`), with `ESP_LOGE("cfgcore", ...)` log.
- `cfgcore::getValue` returns `ESP_ERR_INVALID_ARG` on unknown key (`config_core.cpp:97`).
- `cfgcore::applyTemplateText` returns `ESP_ERR_INVALID_STATE` on template nesting > 8 (`config_core.cpp:121`).
- `cfgcore::applyNamed` returns `ESP_ERR_NOT_FOUND` on unknown template (`config_core.cpp:152`).
- `cfgcore::importJson` / `importXml` return `ESP_ERR_INVALID_ARG` if any key unrecognized (`config_core.cpp:334`, `config_core.cpp:394`).
- `cfgserial::execute` returns `"ERR <message>"` strings on failure (`config_serial.cpp:96`); on unknown command returns `"ERR unknown command"` (`config_serial.cpp:96`).
- Int values are clamped to `[min, max]` via `constrain()` in `writeTyped` (`config_core.cpp:47`) and `load()` (`config_core.cpp:175`, `config_core.cpp:204`).

## 13. Allocation

All data structures are **static** — no dynamic allocation:
- `Config cfg` is a static global (`config_core.cpp:12`).
- `CONFIG_FIELDS[]` and `OUTPUT_FIELDS[]` are `static const` arrays in `.rodata` (`config_schema.cpp:46`, `config_schema.cpp:153`).
- `dmb_output_t` instances are static (allocated by `output_init.cpp`).
- `SeqLock::seq` is a `volatile uint32_t` member (`seqlock.h:12`).
- `RmtDmx.sym` symbol buffer is heap-allocated in DRAM via `heap_caps_malloc(..., MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)` inside `rmtDmxInit` (`dmx_rmt.h:112`), not in the included headers.

On the native host test, `Config cfg` is a single shared global — no `malloc` for the config itself. The `String` class (Arduino shim at `test/native/shim/Arduino.h:23`) uses `std::string` internally.

## 14. Timing

No timing constraints on the headers themselves. The `SeqLock` imposes lock-free semantics: the writer on core 0 is never delayed by the reader on core 1 (`seqlock.h:10`), and `snapshot()` retries up to 8 times (`seqlock.h:22`). If the writer wins all 8 retries, the caller transmits the previous frame (`seqlock.h:19–20`).

## 15. Traceability

| Claim | Evidence |
|---|---|
| `Config` has 47 fields | `config_schema.h:38–89` |
| `DmxOutput` has 24 fields | `config_schema.h:11–36` |
| `cfg` singleton defined in config_core.cpp | `config_core.cpp:12` |
| `MAX_OUTPUTS` defaults to 4 via build flag | `config_schema.h:6–9`, `platformio.ini:51` |
| `CfgField` uses `offsetof(Config, member)` | `config_types.h:29`, `config_schema.cpp:29–36` (IFIELD macro) |
| `writeTyped` clamps to min/max | `config_core.cpp:47` |
| `load()` resolution order: neutral → template → NVS | `config_core.cpp:167–168` |
| Legacy output keys `o0_*`/`o1_*` migrated to `a_*`/`b_*` | `config_core.cpp:182–196` |
| NVS namespace is `"dmxgw"` | `config_core.cpp:15`, `main.cpp:36` |
| `applyTemplateText` nesting cap is 8 | `config_core.cpp:121` |
| `Snapshot` retries 8 times | `seqlock.h:22` |
| `output_mode_t` has 2 values | `output.h:11–14` |
| `resolveOutputMode` returns RDM_FULL if mode==1 or rtsPin>=0 | `output.h:28–32` |
| `dmx_output_t.seq` is `volatile` | `output.h:25` |
| RDM structs are field-by-field parsed, not memcpy'd | `rdm_types.h:13–21` |
| PID constants are plain `uint16_t`, not enum | `rdm_types.h:85` |
| `RDM_UID_MAX` used as DISC_UNIQUE_BRANCH upper bound | `rdm_types.h:54` |
| `eth_phy.h` guarded by `HAS_ETH_RMII` | `eth_phy.h:6` |
| IP101 maps to `ETH_PHY_TLK110` | `eth_phy.h:15` |
| `DMX_PACKET_SIZE` is 513 (start code + 512) | `rdm_types.h:33` |
| `RDM_ASCII_SIZE_MAX` is 33 (32 + NUL) | `rdm_types.h:37` |
| `CFG_REBOOT` / `CFG_LIVE` flag values | `config_types.h:18`, `config_types.h:22` |
| `CFG_SECRET` masks values in dump | `config_core.cpp:236` |
| Config tables use `AR SZ` macro | `config_schema.cpp:9` |
| Native test shim provides `String` over `std::string` | `test/native/shim/Arduino.h:23` |

## 16. Cross-References

- [config-engine](./config-engine.md) — consumes `Config`, `DmxOutput`, `CfgField`, `CfgOutputField`, all enums; implements load/save/dump/import
- [core-dmx-buffer](./core-dmx-buffer.md) — instantiates `SeqLock` for cross-core DMX frame buffering
- [core-rdm-engine](./core-rdm-engine.md) — consumes `rdm_uid_t`, `rdm_ack_t`, PID constants, `rdm_response_type_t`
- [core-output-init](./core-output-init.md) — instantiates `dmx_output_t`, calls `resolveOutputMode()`
- [core-rdm-task](./core-rdm-task.md) — consumes `rdm_ack_t`, `dmx_err_t`, PID constants
- [core-rdm-discovery](./core-rdm-discovery.md) — uses `RDM_UID_MAX`, `RDM_UID_BROADCAST_ALL` for DISC_UNIQUE_BRANCH bounds
- [drv-dmx-rmt-tx](./drv-dmx-rmt-tx.md) — `dmx_output_t.rmt` holds the `RmtDmx` instance; `DMX_PACKET_SIZE` from `rdm_types.h:33`
- [net-ethernet](./net-ethernet.md) — calls `rmiiPhyType()` and reads `RMMI_PHY_NAMES`
- [sys-led-status](./sys-led-status.md) — consumes `ledPin`, `ledType`, `ledRgb`, `ledBrR..ledBrW`
- [sys-crash-guard](./sys-crash-guard.md) — `nvs_migrate` scene key namespacing depends on `Scene` struct from `scene_engine.h`

## 17. Limitations

- `rdm_pid_t` is a plain `uint16_t`, not an enum — no compile-time exhaustiveness checking for manufacturer-specific PIDs (`rdm_types.h:85`).
- Only `DMX_OK` is defined in `dmx_err_t` — no error codes for failure modes (`rdm_types.h:131`). The comment at `rdm_types.h:128` explains this is intentional but limits diagnostic granularity.
- ESP32-S3 has only 4 RMT TX channels — `OUTPUT_FIELD_COUNT` per output is unbounded by hardware; the `#warning` at `config_schema.h:94` fires if `CONFIG_LUXDMX_MAX_OUTPUTS > 4`.
- IP101 PHY is mapped to `ETH_PHY_TLK110` driver — "pin-compatible" per `eth_phy.h:15` but not guaranteed identical across all IP101 variants.
- `RdmPhyType` falls through to `ETH_PHY_LAN8720` default for unknown indices (`eth_phy.h:20`).

## 18. Open Questions

- Not determinable from the inspected source code — whether `CONFIG_LUXDMX_MAX_OUTPUTS` can be set > 4 at the build-system level and what the runtime behavior would be (the `#warning` only fires at preprocessor time; no runtime guard exists in the headers).
- Not determinable from the inspected source code — whether the `volatile uint32_t seq` field in `dmx_output_t` (`output.h:25`) is ever read by core 0 for any purpose other than the WebSocket frame-differencing path (no consumer code was inspected in this module's scope).

## 19. Testing

- **Native smoke test**: `test/native/config_test.cpp` — tests template resolution, setValue/getValue round-trip, save/load NVS round-trip, and serial console grammar (`config_test.cpp:11–55`).
- **Unity unit tests**: `test/unit-test/test_config/test_unit_config.cpp` — 8 tests covering template defaults, set/get, NVS round-trip, luxdmx_4uni template, and serial commands (`test_unit_config.cpp:13–87`).
- **Unity unit tests**: `test/unit-test/test_rdm_types/test_unit_rdm_types.cpp` — tests UID pack/unpack, PID constants, enum values (`rdm_types.h` consumers).
- **Native seqlock test**: `test/native/seqlock_test.cpp` — tests `SeqLock::snapshot` under concurrent write (`include/seqlock.h` consumers).
- **Unity env** builds `cfg/config_core.cpp`, `cfg/config_schema.cpp`, `cfg/config_serial.cpp`, `config_templates_gen.cpp` (`platformio.ini:204–213`).
- No dedicated header self-test — headers are validated only through consuming modules' tests.

## 20. History

- RMT-based DMX TX replaced `esp_dmx` UART path to fix issue #64 (broken breaks under network DMA contention) — referenced in `rdm_types.h:4–6` and `dmx_rmt.h:2`.
- RDM types extracted from `esp_dmx` library into standalone `rdm_types.h` — `rdm_types.h:3–8`.
- `dmx_output_t` gained `port` field for UART number selection; previously RDM RX was implicit — referenced in `output.h:16–26`.
