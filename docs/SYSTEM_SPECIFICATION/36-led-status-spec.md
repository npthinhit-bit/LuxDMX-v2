# LED Status Specification

Domain: sys.led-status

## 1. Module Overview

The LED Status module owns the status-LED hardware abstraction for the gateway. It supports four LED types selected by configuration: **off** (no LED), **single GPIO** (plain digital output with PWM dimming), **WS2812 RGB** (individually-addressable single-pixel strip), and **5-LED panel** (discrete multi-LED array with power/data/output/status indicators). The module configures the hardware PWM peripheral (LEDC) for the single-GPIO path, packs RGB colour into a 32-bit integer (0xRRGGBB), applies brightness scaling, and drives the boot-connecting animation.

The module exposes a single global state struct that holds the desired colour and on/off flag. The scheduler calls it every 50 milliseconds during runtime and during setup. The network-active indication is set by the scheduler's network-receive task as a green colour. On boards with the 5-LED panel, each LED colour channel has an independent brightness knob calibrated per-luminous-output to compensate for differing LED brightness per mA.

Owned: the LEDC PWM channel attachment, the global colour state, the boot-connecting pulse animation.
Delegated: none.
Consumed by: Task Scheduling (50 ms LED tick), System bring-up (setup), WebSocket status (reads current colour).

## 2. External Interfaces

### Caller-Facing API

| Function | Arguments | Behaviour |
|---|---|---|
| init_LED | (void) | Resolves the configured GPIO pin; attaches the hardware PWM channel at 1 kHz with 8-bit duty resolution (256 steps). Sets global on-flag to true. Degraded (LED off, no fault) if the pin is invalid or PWM attach fails. |
| set_LED_color | (rgb: packed 0xRRGGBB, on: boolean) | Stores the colour and on/off flag into global state. When on, extracts R/G/B channels and writes the average luminance to the PWM duty register. When off, sets duty to zero. |
| set_LED_color | (rgb: packed 0xRRGGBB) | Convenience overload; delegates with on=true. |
| set_LED_brightness | (percent: 0-100) | Clamps to 100; maps the percentage linearly to the 8-bit PWM duty range (0-255). Overwrites the current colour duty value. |
| boot_connecting_LED | (void) | Animates a 400 ms blink cycle (200 ms on / 200 ms off) at low luminance to indicate the device is booting and connecting. Uses an internal static timer for phase tracking. |

### Global State

A single state struct holds the live colour:

| Field | Type | Initial | Description |
|---|---|---|---|
| rgb | 32-bit unsigned integer | 0 | Packed 0xRRGGBB colour; high byte unused. |
| on | boolean | false | Whether the LED should emit light. |

### LED Types

| Index | Label | Description |
|---|---|---|
| 0 | off | No LED indicator; initialization is skipped. |
| 1 | plain GPIO | Single LED on a GPIO pin, dimmed via hardware PWM. |
| 2 | WS2812 RGB | Individually-addressable RGB pixel driven via a serial protocol on one GPIO. |
| 3 | 5-LED panel | Discrete LED array with dedicated pins per colour channel, indicating power, data, output, and status. |

### 5-LED Panel Layout

On the LuxDMX v6 PCB and 4uni board, the panel has five discrete LEDs wired to individual GPIO pins, serving the following functional roles: **power**, **data**, **output**, **status**, and **general**. Each colour channel has an independent brightness field to compensate for the differing luminous intensity per mA of green and white LEDs versus red, yellow, and blue.

| LED # | Colour | Config pin | Function |
|---|---|---|---|
| 1 | Red | ledR | Status indicator (error, fault) |
| 2 | Green | ledG | Data / network activity |
| 3 | Yellow | ledY | Output / warning / merge activity |
| 4 | Blue | ledB | Status / configuration mode |
| 5 | White | ledW | Power / general status |

## 3. State Machine

No formal state machine. The LED has two observable output states driven by the global on-flag and colour:

- **Booting**: setup calls set_LED_color with white (dim) — the device is starting up.
- **Network active**: the runtime tick sets a green colour (0x000a00) with the on-flag true, indicating the network is receiving packets.
- **Off**: set_LED_color with on=false drives the PWM duty to zero — the LED is dark.

Brightness is a scalar modifier applied on top of the current colour (the PWM duty register is scaled 0-255). It is not a separate state.

The boot-connecting animation is a transient phase that runs during early boot before network connectivity is established, cycling between on and off every 200 ms for a total 400 ms period.

## 4. Data Flow

1. **Pin resolution**: init_LED reads the configured LED pin; if it is in the valid range (1-48), it overrides the default. The default is the board's built-in LED pin.

2. **PWM attach**: The module attaches the LEDC PWM channel: 1 kHz frequency, 8-bit duty resolution, channel index 0. If the attach fails, a flag is cleared and all subsequent writes are suppressed (LED off, degraded).

3. **Success path**: On successful attach, the on-flag is set to true and the boot white colour is written.

4. **Colour set**: set_LED_color stores the RGB value and on-flag into global state. If off, duty is set to zero. If the PWM channel is not attached, the colour is still stored in the state struct (for status reporting) but no hardware write occurs. If attached, R, G, B are extracted via bit-shift, the average luminance (r+g+b)/3 is computed, and the PWM duty register is written.

5. **Brightness set**: set_LED_brightness clamps the percentage to 100, then maps the 0-100 range linearly to the 0-255 PWM duty range. This overwrites whatever colour duty was previously set.

6. **Runtime tick**: The scheduler's LED task runs every 50 milliseconds. It calls the network-state updater (which sets the green network-active colour), then applies the stored global colour to the PWM hardware.

## 5. Configuration Integration

| Field | CFG flag | Type | Live/Reboot | Description |
|---|---|---|---|---|
| ledPin | CFG_REBOOT | integer (1-48) | Reboot | GPIO pin for the LED. Only consumed by the single-GPIO path. |
| ledType | CFG_REBOOT | enum (0-3) | Reboot | LED type selection: off, plain GPIO, WS2812, 5-LED panel. |
| ledR | CFG_REBOOT | integer | Reboot | Red LED GPIO for the 5-LED panel (status indicator). |
| ledG | CFG_REBOOT | integer | Reboot | Green LED GPIO for the 5-LED panel (data/activity). |
| ledY | CFG_REBOOT | integer | Reboot | Yellow LED GPIO for the 5-LED panel (output warning). |
| ledB | CFG_REBOOT | integer | Reboot | Blue LED GPIO for the 5-LED panel (status mode). |
| ledW | CFG_REBOOT | integer | Reboot | White LED GPIO for the 5-LED panel (power). |
| ledBrR | CFG_LIVE | integer (0-255) | Live | Red LED brightness for the panel. |
| ledBrG | CFG_LIVE | integer (0-255) | Live | Green LED brightness for the panel. |
| ledBrY | CFG_LIVE | integer (0-255) | Live | Yellow LED brightness for the panel. |
| ledBrB | CFG_LIVE | integer (0-255) | Live | Blue LED brightness for the panel. |
| ledBrW | CFG_LIVE | integer (0-255) | Live | White LED brightness for the panel. |

The ledPin field is resolved from config. The ledType field selects the rendering path. The per-colour panel pins and brightness fields configure the 5-LED panel path. On the single-GPIO path, only ledPin is consumed; the panel fields are declared but not exercised by that path.

## 6. Lifecycle

- **Init (core 0, setup)**: init_LED resolves the pin, attaches the PWM channel, and sets the on-flag. Then a white boot colour is written.
- **Runtime (priority 1, 50 ms)**: The scheduler's LED task calls the network-state updater, then applies the global colour to hardware.
- **Boot animation**: boot_connecting_LED runs a 400 ms blink cycle during early boot before network connectivity is established.
- **Shutdown**: None; the PWM channel is never detached; on reboot the channel is re-initialized from scratch.

## 7. Error Handling

- If the PWM channel attach fails, a flag is cleared and init_LED returns with a log message. All subsequent set_LED_color and set_LED_brightness calls early-return (no hardware write). The global state struct is still updated, so the colour is visible to status reporting consumers even when no hardware is attached.
- If the on-flag is false, the PWM duty is set to zero (LED off) regardless of the colour value.
- Brightness is clamped to the 0-100 range; values above 100 are treated as 100.
- No exceptions or error codes are used; all functions are void. The PWM attach return value is checked only in init_LED.

## 8. Timing Constraints

| Item | Value |
|---|---|
| PWM frequency | 1 kHz |
| PWM duty resolution | 8-bit (256 steps) |
| LED task period | 50 ms |
| Boot animation cycle | 400 ms (200 ms on / 200 ms off) |
| init_LED duration | one-shot, sub-millisecond |

The 50 ms LED task period is the only hard cadence; all colour writes to the PWM hardware are sub-microsecond.

## 9. Memory and Allocation Model

- Global state struct, pin/channel/attachment-flag variables: static file-scope, zero-initialized in BSS/DATA.
- Boot animation internal timer: static file-scope uint32.
- No heap allocation. The PWM channel configuration is held by the hardware PWM driver, not by this module.

## 10. Safety Considerations

- The LED module runs on core 0 during setup, before any task is spawned. The LED task runs at the lowest priority (priority 1) and is unpinned: it never competes with the real-time DMX/RDM path on core 1.
- The global colour state is written by the LED API and the network-state updater, then read by the LED task with no lock. The 50 ms cadence and single-word writes make torn reads non-observable on the target hardware.
- A PWM attach failure is degraded (LED off) rather than fatal: the device boots and operates normally without a status indicator.
- The boot animation provides user feedback during the longest unobservable boot phase (WiFi connection), preventing the appearance of a hung device.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| Task Scheduling | downstream (consumer) | LED task (50 ms) calls the colour setter and network-state updater; setup calls init_LED. |
| Config Engine | upstream (producer) | Loads ledPin, ledType, and panel fields before init_LED runs. |
| WebSocket status | downstream (consumer) | Reads the global colour state for the status frame. |
| Crash Guard | downstream | The crash guard writes boot-phase log messages via serial; it does not set the LED. |

## 12. Testing Verification

No host-native test covers the LED module: the PWM attach and write functions require the hardware HAL. A config engine host test verifies that the board template resolves to the 5-LED panel type as a default, but this tests the configuration engine, not the LED driver. The RGB-to-luminance extraction and brightness-to-duty mapping are pure functions that could be extracted and unit-tested but are not in the current test suite. The boot animation and network-active green colour are validated visually during the firmware evaluation workflow.

## 13. Open Questions

1. Where the boot_connecting_LED function is called from: the inspected code shows it is defined but the call site in the boot sequence was not fully traced.
2. Where the set_LED_brightness function is called from: no caller was found in the inspected source.
3. The WS2812 and 5-LED panel rendering paths: the per-colour config fields exist but are not exercised by the single-GPIO code path. It is unclear whether a separate source file implements these or whether they are unimplemented stubs.
4. Whether the global colour state is also consumed by the WebSocket status payload beyond the LED task.

## 14. History

The LED state struct was extracted into a dedicated module during the five-layer architecture rewrite to decouple LED state from the main setup file. The network-state updater was added to the task layer to set the green network-active indicator from the network receive path without crossing core boundaries. The LEDC PWM channel attach API replaced the older setup/attach-pair functions, reflecting an update to the Arduino-esp32 core version. The ledType enum was extended to include the 5-LED panel type for the LuxDMX v6 PCB, but the driver implementation for the panel path lags behind the config support.
