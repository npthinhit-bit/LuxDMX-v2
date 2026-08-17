# Display Specification

Domain: sys.display

## 1. Module Overview

The Display module owns the OLED display subsystem for the gateway. It supports SSD1306 and SH1106 controllers over either I2C or SPI bus, with configurable pins for each bus type. The module provides initialization and a render entry point that the display task calls every 200 milliseconds.

On the development board and CI build environments, the display hardware is not wired, so the module is a no-op stub: initialization sets the display-ready flag to false, and the render function produces no output. On production hardware with an attached OLED, the implementation would initialise the chosen controller, configure the bus, and render a status screen showing firmware version, network state, universe assignment, and per-output frame rate.

Owned: the display-ready flag, the render entry point called by the display task, bus pin configuration.
Delegated: none.
Consumed by: Task Scheduling (display task, 200 ms, conditional on ready flag).

## 2. External Interfaces

### Caller-Facing API

| Function | Arguments | Behaviour |
|---|---|---|
| initDisplay | (void) | Reads the configured display type and bus pins; initialises the selected controller (SSD1306 128x64, SSD1306 128x32, SH1106, or SSD1351 colour). Sets the display-ready flag to true on success, false on failure or when no display is configured. |
| renderDisplay | (void) | Renders the current status screen to the display: firmware version, hostname, IP address, link state, active sender count, and per-output universe/FPS/loss. Called every 200 ms by the display task only when the ready flag is true. |

### Global Flag

| Symbol | Type | Initial | Description |
|---|---|---|---|
| dispReady | boolean | false | Set to true by initDisplay on successful controller initialisation. Gates the display task creation. |

### Display Type Enum

| Index | Label | Controller |
|---|---|---|
| 0 | off | No display; initialisation skipped. |
| 1 | SSD1306 128x64 | Monochrome OLED, 128x64 pixels, I2C or SPI. |
| 2 | SSD1306 128x32 | Monochrome OLED, 128x32 pixels, I2C or SPI. |
| 3 | SH1106 | Monochrome OLED, 128x64 pixels, I2C or SPI. |
| 4 | SSD1351 colour | Colour OLED, 128x128 pixels, SPI. |

### Bus Pin Fields

| Field | Type | Range | Description |
|---|---|---|---|
| dispSda | integer | -1 (disabled) to 48 | I2C SDA pin. |
| dispScl | integer | -1 (disabled) to 48 | I2C SCL pin. |
| dispRot | integer | 0 or 1 | Display rotation flag (0 = normal, 1 = 180 degree rotation). |
| dispCs | integer | -1 (disabled) to 48 | SPI chip-select pin. |
| dispDc | integer | -1 (disabled) to 48 | SPI data-command pin. |
| dispRst | integer | -1 (disabled) to 48 | SPI reset pin. |
| dispSck | integer | -1 (disabled) to 48 | SPI clock pin. |
| dispMosi | integer | -1 (disabled) to 48 | SPI master-out-slave-in pin. |

## 3. State Machine

No formal state machine. The display has three observable conditions:

- **Uninitialised**: initDisplay has not run or returned. dispReady is false. The display task is not created. The display remains dark.
- **Ready**: initDisplay succeeded. dispReady is true. The display task is created and calls renderDisplay every 200 ms. The display shows the status screen.
- **Off**: dispType is 0 (off) or pins are unconfigured. initDisplay is a no-op; dispReady stays false.

There is no runtime transition between Ready and Uninitialised once initDisplay runs during setup.

## 4. Data Flow

1. **Configuration resolution**: The config engine populates the display type and bus pin fields from the board template during setup, before display initialization.

2. **Initialization**: initDisplay reads the dispType field. If it is 0 (off) or no bus pins are configured, it returns immediately without setting the ready flag. If a valid controller type is selected, it attempts to initialise the controller over the configured I2C or SPI bus. On success, dispReady is set to true.

3. **Task creation (conditional)**: During task scheduling setup, if dispReady is true, a display task is created at priority 1 (lowest) with a 4096-byte stack and a 200 ms period. If dispReady is false, no display task is created.

4. **Rendering loop**: The display task calls renderDisplay every 200 ms. renderDisplay reads the current status (firmware version, network state, sender counts, per-output metrics) and draws it to the OLED buffer, then flushes the buffer to the display controller. The render function reads state updated by other tasks (stats, network state), so reads are approximate and may be slightly stale.

5. **No output path**: The display module does not write to any network socket, OTA path, or DMX output. All writes are to the OLED controller over the local bus.

## 5. Configuration Integration

| Field | CFG flag | Type | Live/Reboot | Description |
|---|---|---|---|---|
| dispType | CFG_REBOOT | enum (0-4) | Reboot | Display controller type selection. |
| dispSda | CFG_REBOOT | integer | Reboot | I2C SDA pin. |
| dispScl | CFG_REBOOT | integer | Reboot | I2C SCL pin. |
| dispRot | CFG_LIVE | integer (0-1) | Live | 180-degree rotation flag. |
| dispCs | CFG_REBOOT | integer | Reboot | SPI chip-select pin. |
| dispDc | CFG_REBOOT | integer | Reboot | SPI data-command pin. |
| dispRst | CFG_REBOOT | integer | Reboot | SPI reset pin. |
| dispSck | CFG_REBOOT | integer | Reboot | SPI clock pin. |
| dispMosi | CFG_REBOOT | integer | Reboot | SPI MOSI pin. |

All bus pin and controller type fields require a reboot to take effect, because the display controller is initialised once during setup. The rotation flag is live and can be toggled without restart.

## 6. Lifecycle

- **Init phase (core 0, setup)**: initDisplay is called. On success, dispReady is set to true. On dev boards without hardware, it is a no-op and dispReady remains false.
- **Task creation**: The scheduler creates the display task only if dispReady is true, at priority 1 with a 4096-byte stack and 200 ms period.
- **Runtime**: The display task calls renderDisplay every 200 ms. renderDisplay reads global state and draws the status screen.
- **Shutdown**: None; the display remains active until power-off or reboot. There is no explicit teardown.

## 7. Error Handling

| Condition | Handling |
|---|---|
| dispType is 0 (off) | initDisplay returns without initialising; dispReady stays false; no display task created. |
| Bus pins unconfigured (-1) | initDisplay treats this as no display; returns without error; dispReady stays false. |
| Controller initialisation failure | initDisplay sets dispReady to false and logs a warning; no display task is created. |
| Bus communication error during render | renderDisplay skips the frame; the display retains the last drawn buffer. No crash or restart. |
| I2C address not found | initDisplay fails gracefully; dispReady stays false. |
| SPI bus not responding | initDisplay fails gracefully; dispReady stays false. |

No exceptions are used. All functions return void. Error visibility is limited to boot-phase log messages.

## 8. Timing Constraints

| Item | Value |
|---|---|
| Display task period | 200 ms |
| Display task priority | 1 (lowest) |
| Display task stack | 4096 bytes |
| Display init duration | one-shot, sub-second |

The 200 ms render period is the only cadence. The display task runs at the lowest priority and is unpinned, so it never competes with the real-time DMX/RDM path. Display updates are best-effort; a missed render due to a higher-priority task is acceptable.

## 9. Memory and Allocation Model

- dispReady: static file-scope boolean, zero-initialized in BSS.
- No heap allocation in the module stub form. On production hardware with a full controller driver, the OLED library may allocate a display buffer (typically 1 KB for a 128x64 monochrome display) on the Arduino heap, but this is managed by the driver library, not by this module.
- The render function uses a stack-local frame buffer for drawing operations; the size depends on the controller resolution.

## 10. Safety Considerations

- The display module runs at priority 1 (lowest), unpinned. It never preempts the real-time DMX transmit task (priority 19) or the RDM task (priority 18) on core 1.
- initDisplay is called during setup on core 0, before any task is spawned. A display controller failure cannot brick the device; it simply leaves dispReady false and the status screen is not shown.
- The display task is only created when dispReady is true, so a failed or missing display consumes no runtime resources.
- The 200 ms render period is slow enough that display rendering never blocks the network or DMX paths. Render reads of shared state are approximate; a slightly stale status display is preferable to a missed DMX frame.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| Task Scheduling | upstream (consumer) | Creates the display task conditionally on dispReady; calls renderDisplay in the 200 ms tick. |
| Config Engine | upstream (producer) | Loads dispType and bus pin fields before initDisplay runs. |
| Stats | upstream (consumer) | Provides uptime, frame counts, FPS, sender count, source status for the status screen. |
| Network state | upstream (consumer) | Provides link state, IP address, hostname for the status screen. |
| Firmware version | upstream (consumer) | Provides the version string shown on the status screen. |

## 12. Testing Verification

No host-native test covers the display module; the OLED controller initialisation, bus communication, and render functions require hardware-specific libraries that are not shimmed in the native test environment. The module stub form is validated only by the firmware evaluation workflow, which confirms that the boot serial log shows no display errors and that the display task is not created when no hardware is present.

## 13. Open Questions

1. Whether the full production display driver (beyond the stub) is implemented in a separate source file or is still pending migration from the v1 monolith.
2. Whether the display task is created on a specific core or left unpinned to whichever core is free.
3. Whether renderDisplay accesses shared stats/network state with any synchronization, or whether it relies on the low-priority, best-effort nature of the display to tolerate stale reads.

## 14. History

The display module was introduced as a stub during the five-layer architecture rewrite, with renderDisplay left empty pending the display driver migration from the monolithic v1 firmware. The display-ready flag was added to the task scheduler so that the display task is only created when hardware is actually present, avoiding wasted resources on dev boards and CI builds. The display type enum was extended to cover SSD1306 (128x64 and 128x32), SH1106, and SSD1351 colour controllers, with separate I2C and SPI pin fields to support both bus types.
