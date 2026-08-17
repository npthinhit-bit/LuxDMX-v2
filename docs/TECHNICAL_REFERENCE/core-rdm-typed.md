# RDM Typed — Technical Reference

Domain: core.rdm_typed

## 1. Domain Scope

The RDM Typed module implements typed E1.20 GET/SET wrappers for the RDM PIDs this controller supports. Each function builds a request with `rdmTransaction()`, waits for the ack, and parses the parameter-data bytes into a typed struct or scalar value. It owns:

- **GET wrappers** — `rdmOpDeviceInfo`, `rdmOpSwLabel`, `rdmOpSensorDef`, `rdmOpSensorVal`, `rdmOpGetString`, `rdmOpGetMode`, `rdmOpGetModes`, `rdmOpGetIdentifyMode`, `rdmOpGetDeviceHours`, `rdmOpGetDevicePower`, `rdmOpGetBurnIn`, `rdmOpGetPersonalityDescription`, `rdmOpGetSensorFull`, `rdmOpGetStatus`.
- **SET wrappers** — `rdmOpSetAddr`, `rdmOpSetIdentify`, `rdmOpSetString`, `rdmOpSetPersonality`, `rdmOpSetMode`, `rdmOpSetIdentifyMode`, `rdmOpSetDevicePower`, `rdmOpSetBurnIn`, `rdmOpSensorRecord`.
- **Derived** — `rdmSubDeviceCount` (`rdm_typed.cpp:131-136`) which wraps `rdmOpDeviceInfo` and returns the sub-device count.

It delegates transport to the blocking `rdmTransaction()` in [core-rdm-task](./core-rdm-task.md) (`rdm_task.cpp:304`), which dispatches to the core-1 RDM task. It uses types from [core-rdm-engine](./core-rdm-engine.md) (`rdm_types.h` structs and the `rdm_uid_t`, `rdm_ack_t`, `rdm_response_type_t` enums) and PID constants from [include-headers](./include-headers.md) (`rdm_types.h`).

It is consumed by `ws_handler.cpp` (WebSocket RDM commands) and the Art-Net bridge.

## 2. Layer Mapping

| Layer | Module path | Role |
|---|---|---|
| **drv** | (indirect via `rdmTaskInit`) | Not called directly by this module. |
| **cfg** | `include/config_schema.h` | `MAX_SCENES` and `MAX_OUTPUTS` available transitively through `rdm_engine.h:10`. |
| **core** | `src/core/rdm_typed.cpp` | **This module** — typed PID wrappers. |
| **net** | `src/net/ota.cpp`, `src/net/ws_handler.cpp` | Calls typed wrappers for web/OTA RDM operations. |
| **sys** | — | Not applicable. |

## 3. Source Files

| File | Path | Role |
|---|---|---|
| `rdm_typed.cpp` | `src/core/rdm_typed.cpp` | All typed GET/SET wrappers. |

The declarations for these wrappers live in `rdm_engine.h` (`rdm_engine.h:76-112`); this module implements them.

## 4. Data Structures

This module does not define its own persistent structs. It operates on types declared in [core-rdm-engine](./core-rdm-engine.md):

| Type | Defined at | Used by |
|---|---|---|
| `rdm_uid_t` | `rdm_types.h:43-46` | All wrappers take `const rdm_uid_t& uid`. |
| `rdm_ack_t` | `rdm_types.h:136-160` | All wrappers populate `rdm_ack_t* ack`. |
| `rdm_device_info_t` | `rdm_types.h:174-187` | `rdmOpDeviceInfo` writes to `rdm_device_info_t* i` (`rdm_typed.cpp:3`). |
| `rdm_sensor_definition_t` | `rdm_types.h:190-212` | `rdmOpSensorDef` writes to `rdm_sensor_definition_t* d` (`rdm_typed.cpp:31`). |
| `rdm_sensor_value_t` | `rdm_types.h:216-222` | `rdmOpSensorVal` sets `present_value` (`rdm_typed.cpp:48`). |
| `rdm_response_type_t` | `rdm_types.h:116-123` | Every wrapper checks `ack->type != RDM_RESPONSE_TYPE_ACK`. |

### Stack buffers (`rdm_typed.cpp`)

Each wrapper declares a stack-local parameter-data buffer:

| Function | Buffer | Size | Line |
|---|---|---|---|
| `rdmOpDeviceInfo` | `pd` | 40 | `rdm_typed.cpp:4` |
| `rdmOpSwLabel` | `pd` | 40 | `rdm_typed.cpp:21` |
| `rdmOpSensorDef` | `pd` | 40 | `rdm_typed.cpp:32` |
| `rdmOpSensorVal` | `pd` | 16 | `rdm_typed.cpp:43` |
| `rdmOpSetAddr` | `resp` | 8 | `rdm_typed.cpp:54` |
| `rdmOpSetIdentify` | `resp` | 8 | `rdm_typed.cpp:62` |
| `rdmOpGetString` | `pd` | 40 | `rdm_typed.cpp:69` |
| `rdmOpSetString` | `resp` | 8 | `rdm_typed.cpp:79` |
| `rdmOpSetPersonality` | `resp` | 8 | `rdm_typed.cpp:87` |
| `rdmOpGetSensorFull` | `pd` | 16 | `rdm_typed.cpp:95` |
| `rdmOpGetStatus` | `pd` | 80 | `rdm_typed.cpp:109` |
| `rdmOpGetMode` | `pd` | 40 | `rdm_typed.cpp:141` |
| `rdmOpSetMode` | `pd` | 2 | `rdm_typed.cpp:150` |
| `rdmOpGetModes` | `pd` | 40 | `rdm_typed.cpp:157` |
| `rdmOpGetIdentifyMode` | `pd` | 40 | `rdm_typed.cpp:167` |
| `rdmOpSetIdentifyMode` | `pd` | 2 | `rdm_typed.cpp:176` |
| `rdmOpGetDeviceHours` | `pd` | 40 | `rdm_typed.cpp:183` |
| `rdmOpGetDevicePower` | `pd` | 40 | `rdm_typed.cpp:193` |
| `rdmOpSetDevicePower` | `pd` | 2 | `rdm_typed.cpp:202` |
| `rdmOpGetBurnIn` | `pd` | 40 | `rdm_typed.cpp:209` |
| `rdmOpSetBurnIn` | `pd` | 2 | `rdm_typed.cpp:218` |
| `rdmOpGetPersonalityDescription` | `pd` | 64 | `rdm_typed.cpp:226` |
| `rdmOpSensorRecord` | `pd` | 2 | `rdm_typed.cpp:238` |

## 5. Concurrency

All typed wrappers execute on the **caller's core** — typically core 0 via `ws_handler.cpp` (`main.cpp:150` → `rdmWsProcessQueued`). They call the blocking `rdmTransaction()` (`rdm_task.h:101`, implemented at `rdm_task.cpp:304`), which enqueues the RDM I/O to the core-1 task and blocks on a binary semaphore until completion (`rdm_task.cpp:315`). The actual RMT TX / UART RX occurs on core 1 (`rdm_task.h:13`, `rdm_task.cpp:157`).

The typed wrappers themselves have **no shared mutable state** beyond the stack buffers described in Table 4-1. No locks are used; the blocking wrapper's semaphore serialises access to the single RDM channel.

## 6. State Machine

**No state machine — stateless request/response.** Each wrapper performs one GET or SET and returns. No session or connection state is maintained between calls.

## 7. Entry Points

| Function | Declared | Implemented | Called from |
|---|---|---|---|
| `rdmOpDeviceInfo` | `rdm_engine.h:76` | `rdm_typed.cpp:3` | `ws_handler.cpp` (device info display). |
| `rdmOpSwLabel` | `rdm_engine.h:77` | `rdm_typed.cpp:20` | `ws_handler.cpp`. |
| `rdmOpSensorDef` | `rdm_engine.h:78` | `rdm_typed.cpp:31` | `ws_handler.cpp`. |
| `rdmOpSensorVal` | `rdm_engine.h:79` | `rdm_typed.cpp:42` | `ws_handler.cpp`. |
| `rdmOpSetAddr` | `rdm_engine.h:80` | `rdm_typed.cpp:52` | `ws_handler.cpp:117`. |
| `rdmOpSetIdentify` | `rdm_engine.h:81` | `rdm_typed.cpp:60` | `ws_handler.cpp:123`. |
| `rdmOpSetString` | `rdm_engine.h:83` | `rdm_typed.cpp:78` | `ws_handler.cpp:133`. |
| `rdmOpSetPersonality` | `rdm_engine.h:84` | `rdm_typed.cpp:86` | `ws_handler.cpp:128`. |
| `rdmOpGetSensorFull` | `rdm_engine.h:85` | `rdm_typed.cpp:93` | `ws_handler.cpp`. |
| `rdmOpGetStatus` | `rdm_engine.h:87` | `rdm_typed.cpp:106` | `ws_handler.cpp`. |
| `rdmOpGetMode` | `rdm_engine.h:93` | `rdm_typed.cpp:140` | Not found in inspected callers. |
| `rdmOpGetModes` | `rdm_engine.h:96` | `rdm_typed.cpp:156` | Not found in inspected callers. |
| `rdmOpGetIdentifyMode` | `rdm_engine.h:98` | `rdm_typed.cpp:166` | Not found in inspected callers. |
| `rdmOpGetDeviceHours` | `rdm_engine.h:101` | `rdm_typed.cpp:182` | Not found in inspected callers. |
| `rdmOpGetDevicePower` | `rdm_engine.h:103` | `rdm_typed.cpp:192` | Not found in inspected callers. |
| `rdmOpSetDevicePower` | `rdm_engine.h:104` | `rdm_typed.cpp:201` | Not found in inspected callers. |
| `rdmOpGetBurnIn` | `rdm_engine.h:106` | `rdm_typed.cpp:208` | Not found in inspected callers. |
| `rdmOpSetBurnIn` | `rdm_engine.h:107` | `rdm_typed.cpp:217` | Not found in inspected callers. |
| `rdmOpGetPersonalityDescription` | `rdm_engine.h:109` | `rdm_typed.cpp:224` | Not found in inspected callers. |
| `rdmOpSensorRecord` | `rdm_engine.h:112` | `rdm_typed.cpp:237` | Not found in inspected callers. |
| `rdmSubDeviceCount` | `rdm_engine.h:119` | `rdm_typed.cpp:131` | Not found in inspected callers. |

## 8. Data Flow

Every wrapper follows the same pattern (`rdm_typed.cpp:1`):

1. **Call** — a consumer (typically `ws_handler.cpp`) invokes a typed wrapper, passing a destination UID, an output struct/pointer, and an `rdm_ack_t*`.
2. **Transaction** — the wrapper calls `rdmTransaction(uid, CC, PID, reqPd, reqPdl, respPd, respMax, &respPdl, ack)` (`rdm_task.h:101`, implemented at `rdm_task.cpp:304`), blocking up to 5000 ms (`rdm_task.cpp:315`).
3. **Response check** — every GET wrapper verifies `ack->type == RDM_RESPONSE_TYPE_ACK` and a minimum `pdl` before parsing (`rdm_typed.cpp:7,25,35,46,72,87,98,113,144,160,170,186,196,212,229`).
4. **Parse** — the wrapper extracts big-endian fields from the `pd[]` buffer into the output struct/scalar using explicit shifts.
5. **Return** — `true` on success, `false` if the transaction failed or the response type was not ACK.

### Example: `rdmOpDeviceInfo` (`rdm_typed.cpp:3-18`)

```
GET DEVICE_INFO (0x0060) → rdmTransaction → ack
  pd[2..3]   → model_id        (rdm_typed.cpp:8)
  pd[4..5]   → product_category (rdm_typed.cpp:9)
  pd[6..9]   → software_version_id (rdm_typed.cpp:10)
  pd[10..11] → footprint       (rdm_typed.cpp:11)
  pd[12]     → personality.current (rdm_typed.cpp:12)
  pd[13]     → personality.count   (rdm_typed.cpp:13)
  pd[14..15] → dmx_start_address   (rdm_typed.cpp:14)
  pd[16..17] → sub_device_count    (rdm_typed.cpp:15)
  pd[18]     → sensor_count        (rdm_typed.cpp:16)
```

Requires `pdl >= 19` (the 19-byte DEVICE_INFO payload, `rdm_typed.cpp:7`), matching `rdm_types.h:19`.

### Example: `rdmOpGetStatus` (`rdm_typed.cpp:106-129`)

Parses NACK status messages where each message is 9 bytes (`pdl / 9`, `rdm_typed.cpp:114`); selects the highest-severity message by response type byte at `pd[i*9+2]` (`rdm_typed.cpp:119`).

## 9. Protocol Layout

### Parameter Data layouts (parsed big-endian)

| PID | Wrapper | PDL min | Fields (offset in pd[]) |
|---|---|---|---|
| `DEVICE_INFO` (0x0060) | `rdmOpDeviceInfo` | 19 | model_id(2-3), product_category(4-5), software_version_id(6-9), footprint(10-11), personality.current(12), personality.count(13), dmx_start_address(14-15), sub_device_count(16-17), sensor_count(18) |
| `DEVICE_MODE` (0x1101) | `rdmOpGetMode` | 1 | mode(0) |
| `DEVICE_MODES` (0x1100) | `rdmOpGetModes` | 1 | current(0), count(1, if present) |
| `IDENTIFY_MODE` (0x1011) | `rdmOpGetIdentifyMode` | 1 | mode(0) |
| `DEVICE_HOURS` (0x1010) | `rdmOpGetDeviceHours` | 4 | hours(0-3) |
| `DEVICE_POWER` (0x1012) | `rdmOpGetDevicePower` | 1 | power(0) |
| `BURN_IN` (0x1013) | `rdmOpGetBurnIn` | 1 | minutes(0) |
| `SENSOR_DEFINITION` (0x0200) | `rdmOpSensorDef` | 13 | num(0), type(1), unit(2), prefix(3), range.min(4-5), range.max(6-7), normal.min(8-9), normal.max(10-11), description(13+) |
| `SENSOR_VALUE` (0x0201) | `rdmOpSensorVal` | 3 | sensor_num(0), present_value(1-2) |
| `STATUS_MESSAGE` (0x0030) | `rdmOpGetStatus` | variable | 9 bytes per message: type, ... |
| `DMX_START_ADDRESS` (0x00f0) | `rdmOpSetAddr` | 0 (SET) | addr hi/lo |
| `IDENTIFY_DEVICE` (0x1000) | `rdmOpSetIdentify` | 0 (SET) | on/off (1 byte) |
| `DMX_PERSONALITY` (0x00e0) | `rdmOpSetPersonality` | 0 (SET) | personality index |
| `DMX_PERSONALITY_DESCRIPTION` (0x00e1) | `rdmOpGetPersonalityDescription` | 3 | footprint(0-1), description(2+) |
| `SENSOR_RECORD` (0x0202) | `rdmOpSensorRecord` | 0 (SET) | sensor number |
| `DEVICE_LABEL` (0x0082) | `rdmOpSetString` | 0 (SET) | label string |
| `SW_VERSION_LABEL` (0x00c0) | `rdmOpSwLabel` | variable | label string |

All field offsets verified against `rdm_typed.cpp:8,9,10,11,12,13,14,15,16,26,27,36-39,48,49,99-102,118,122-126,145,161-162,166,171,187-188,197,213,230`.

## 10. Config Integration

No `Config` fields are read directly by this module. The `cfg.rdmMaxDev` ceiling is applied by the [core-rdm-task](./core-rdm-task.md) blocking wrapper (`rdm_task.cpp:323`), not by the typed wrappers.

## 11. Lifecycle

**Stateless.** No `init()` or `deinit()` functions. Each wrapper is a standalone function called on demand. No periodic task hook.

## 12. Error Handling

All wrappers return `bool`:

| Failure mode | Handling | Line |
|---|---|---|
| `rdmTransaction` returns false (timeout / queue down) | Wrapper returns `false` immediately | `rdm_typed.cpp:5,24,34,45,56,64,71,82,89,97,113,145,154,162,172,186,206,222,242` |
| `ack->type != RDM_RESPONSE_TYPE_ACK` | Wrapper returns `false` | `rdm_typed.cpp:7,25,35,46,72,87,98,113,144,160,170,186,196,212,229` |
| `pdl` below minimum | Wrapper returns `false` | `rdm_typed.cpp:7` (pdl < 19), `rdm_typed.cpp:35` (pdl < 13), `rdm_typed.cpp:46` (pdl < 3), `rdm_typed.cpp:98` (pdl < 3), `rdm_typed.cpp:113` (type != ACK only), `rdm_typed.cpp:144` (pdl < 1), `rdm_typed.cpp:160` (pdl < 1), `rdm_typed.cpp:170` (pdl < 1), `rdm_typed.cpp:186` (pdl < 4), `rdm_typed.cpp:196` (pdl < 1), `rdm_typed.cpp:212` (pdl < 1), `rdm_typed.cpp:229` (pdl < 3) |

No logging is performed; errors are propagated as `false` returns.

## 13. Allocation

All buffers are **stack-local** (see Table 4-1). No heap or PSRAM allocation occurs. The largest allocation is `pd[80]` in `rdmOpGetStatus` (`rdm_typed.cpp:109`).

## 14. Timing

| Constraint | Value | Source |
|---|---|---|
| Per-transaction timeout | 5000 ms | `rdm_task.cpp:315` (inherited from blocking wrapper) |
| Per-attempt RDM retry | 1 ms | `rdm_task.cpp:32` (called inside `rdmTransaction`) |
| MAX per-string copy | 32 bytes | `rdm_typed.cpp:80` (`strlen` clamp), `rdm_typed.cpp:27` (pdl clamp), `rdm_typed.cpp:73` (len clamp), `rdm_typed.cpp:231` (descLen clamp) |

## 15. Traceability

| Claim | Evidence |
|---|---|
| All wrappers include only `rdm_engine.h`. | `rdm_typed.cpp:1` |
| `rdmOpDeviceInfo` checks `pdl < 19`. | `rdm_typed.cpp:7` |
| `rdmOpSwLabel` clamps `cn` to `n-1`. | `rdm_typed.cpp:26` |
| `rdmOpSensorDef` checks `pdl < 13` and clamps description to 32 bytes. | `rdm_typed.cpp:35,37-38` |
| `rdmOpSensorVal` reads `present_value` from `pd[1..2]`. | `rdm_typed.cpp:48` |
| `rdmOpGetSensorFull` guards each optional field with a `pdl >=` check. | `rdm_typed.cpp:99-102` |
| `rdmOpGetStatus` parses 9-byte status messages (`pdl / 9`). | `rdm_typed.cpp:114` |
| `rdmOpGetStatus` selects the highest-severity message by type byte. | `rdm_typed.cpp:117-119` |
| `rdmSubDeviceCount` wraps `rdmOpDeviceInfo`. | `rdm_typed.cpp:134` |
| Extended PIDs start at `rdmOpGetMode` (0x1101). | `rdm_typed.cpp:138` |
| All SET wrappers check `ack->type == RDM_RESPONSE_TYPE_ACK`. | `rdm_typed.cpp:57,65,83,90,153,179,205,221,241` |
| `rdmOpSetString` clamps the string to 32 bytes. | `rdm_typed.cpp:80` |
| `rdmOpGetPersonalityDescription` clamps the description to `descLen-1`. | `rdm_typed.cpp:231` |
| All wrappers call `rdmTransaction` (blocking, 5 s timeout). | `rdm_typed.cpp:5,23,33,44,55,63,71,81,88,96,111,134,142,151,158,168,177,184,194,203,210,219,227` |

## 16. Cross-References

- **[core-rdm-engine](./core-rdm-engine.md)** — provides `rdm_transaction` declaration (`rdm_engine.h:69`) and all RDM type constants used by the wrappers.
- **[core-rdm-task](./core-rdm-task.md)** — implements the blocking `rdmTransaction()` wrapper (`rdm_task.cpp:304`) that all typed functions call.
- **[include-headers](./include-headers.md)** — documents `rdm_types.h` PID constants (`RDM_PID_DEVICE_INFO`, `RDM_PID_IDENTIFY_DEVICE`, etc.) and the `rdm_ack_t` struct.
- **[net-websocket-handler](./net-websocket-handler.md)** — `handleWsTextRdm()` in `ws_handler.cpp:41` dispatches WebSocket RDM commands to these wrappers; `rdmWsProcessQueued()` (`ws_handler.cpp:93`) executes them on core 0 from `main.cpp:150`.

## 17. Limitations

- **No RDM_SC_SUB validation** — the wrappers do not verify the response sub-start code; they rely on `rdmTransaction`/`rdmReadResp` for framing (`rdm_engine.cpp:131-175`).
- **Fixed buffer sizes** — each wrapper uses a fixed-size stack buffer (e.g. `pd[40]`); a responder returning more data than expected would be truncated (`rdm_typed.cpp:4,21,32,43,69,109,141,157,167,183,193,209,210,226`).
- **ACK-only support** — there is no handling for `RDM_RESPONSE_TYPE_ACK_TIMER` (0x01) or `RDM_RESPONSE_TYPE_ACK_OVERFLOW` (0x03); the wrappers return `false` if the type is not `ACK` (`rdm_types.h:117-122`).
- **No batch queries** — each PID requires a separate blocking transaction (5 s timeout each), so multi-PID reads are sequential, not pipelined.
- **`rdmOpGetStatus` severity selection** — only the highest `type` value is selected (`rdm_typed.cpp:119`); E1.20 status messages have specific severity ordering (0=info, 1=warning, 2=error, 3=info, 4=critical) that this simple max comparison does not distinguish.

## 18. Open Questions

- Not determinable from the inspected source code — which `rdmOp*` wrappers are called from `ws_handler.cpp` beyond `rdmOpSetAddr` (`ws_handler.cpp:117`), `rdmOpSetIdentify` (`ws_handler.cpp:123`), `rdmOpSetPersonality` (`ws_handler.cpp:128`), and `rdmOpSetString` (`ws_handler.cpp:133`); the remaining 16 wrappers have no verified callers in the inspected files.
- Not determinable from the inspected source code — how `rdmOpGetStatus` status-message type values (info/warning/error/critical) map to the `type` byte at `pd[i*9+2]` (`rdm_typed.cpp:118`).
- Not determinable from the inspected source code — whether the extended PID wrappers (0x1010–0x1101) are tested against real fixtures; no host or unit tests cover them.

## 19. Testing

- **Native host test** — `test/native/rdm_types_test.cpp` and `test/unit-test/test_rdm_types/test_unit_rdm_types.cpp` cover `rdm_types.h` constants (PID values, command-class enums, response-type enums, sensor type/unit enums). These are the type definitions used by the typed wrappers but do not test the wrapper logic itself.
- **Unity unit test** — `test/unit-test/test_rdm_types/test_unit_rdm_types.cpp` mirrors the native tests with Unity assertions.
- No tests exist for any `rdmOp*()` wrapper function. No hardware-in-loop scripts for SET/GET round-trip validation were found.

## 20. History

- Typed wrappers were added as thin parsing layers over `rdmTransaction()`, replacing direct raw buffer manipulation in callers — all function bodies are pure parse logic delegating to the blocking wrapper (`rdm_typed.cpp:1`).
- The extended PID set (DEVICE_HOURS, DEVICE_POWER, BURN_IN, IDENTIFY_MODE, DEVICE_MODE, DEVICE_MODES, SENSOR_RECORD, PERSONALITY_DESCRIPTION) was added per the feature audit (`rdm_typed.cpp:138`); all follow the same GET/SET + ACK-check pattern as the core wrappers.
- `rdmSubDeviceCount()` was added at `rdm_typed.cpp:131` to wrap `rdmOpDeviceInfo` for sub-device enumeration.
