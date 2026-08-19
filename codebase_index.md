# LuxDMX-v2 ESP-IDF Codebase Index

## Project Overview

LuxDMX-v2 is an Art-Net/sACN â†’ DMX512 gateway firmware for ESP32/ESP32-S3 with full RDM support, OTA updates, and web configuration interface. This version is a **pure ESP-IDF implementation** with PlatformIO integration, replacing the previous hybrid Arduino/ESP-IDF approach.

**Target Hardware:**
- ESP32-S3-WROOM-2 N16R8
- WT32-ETH01
- ESP32-DevKit (esp32dev)

**Key Features:**
- Art-Net 4 and sACN (E1.31) protocol support
- DMX512 output with RMT peripheral
- RDM (E1.20) controller functionality
- Web-based configuration and monitoring
- OTA updates with Ed25519 signature verification
- Multi-board support with hardware abstraction

## Component Architecture

```
components/
â”œâ”€â”€ lux_common/      # Common utilities and base classes
â”œâ”€â”€ lux_hw/          # Hardware abstraction layer
â”œâ”€â”€ lux_wifi/        # WiFi and network management
â”œâ”€â”€ lux_led/         # LED drivers and status indication
â”œâ”€â”€ lux_config/      # Configuration engine
â”œâ”€â”€ lux_core/        # DMX/RDM core logic (Phase 2+)
â”œâ”€â”€ lux_net/         # Network protocols (Phase 2+)
â”œâ”€â”€ lux_web/         # Web server and frontend
â”œâ”€â”€ lux_log/         # Structured logging
â””â”€â”€ lux_test/        # Test utilities and shims
```

## Key Architectural Decisions

### 1. Core Affinity Strategy
- **Core 0**: Network stack, WiFi, AsyncTCP, web server, serial console
- **Core 1**: DMX transmit, RDM service, merge engine (time-critical operations)
- **Rationale**: Prevent network activity from preempting time-critical DMX/RDM operations

### 2. Configuration System
- **Schema-driven**: Single field table drives NVS, web form, and serial console
- **Resolution order**: Neutral values â†’ board template â†’ NVS overlay
- **Live vs reboot**: Clear separation of fields that apply instantly vs require restart
- **Validation**: Range clamping and hardware constraint checking

### 3. Hardware Abstraction
- **Board-specific configurations**: Pin mappings, peripheral assignments, LED types
- **Peripheral interfaces**: GPIO, LED drivers, network interfaces, storage
- **Auto-detection**: Board identification and automatic configuration

### 4. Build System
- **ESP-IDF v5.2**: Standardized build environment
- **PlatformIO integration**: Multi-board support and development tooling
- **Kconfig**: Configuration options with dependencies and validation
- **Template generation**: Build-time generation of configuration defaults and web assets

### 5. Testing Strategy
- **Unit tests**: Component-level testing with Unity framework
- **Integration tests**: Component interaction testing
- **Hardware-in-the-loop**: Real device testing with automated verification
- **Static analysis**: Code quality and security checking
- **CI/CD pipeline**: Automated build, test, and verification

## Component Index

| Component       | Description                          | Key Files                          | Status      |
|-----------------|--------------------------------------|------------------------------------|-------------|
| lux_common      | Common utilities and base classes    | include/common.h, src/utils.c      | Phase 1     |
| lux_hw          | Hardware abstraction layer           | include/hw.h, boards/*             | Phase 1     |
| lux_wifi        | WiFi and network management          | include/wifi_manager.h             | Phase 1     |
| lux_led         | LED drivers and status indication    | include/led_driver.h               | Phase 1     |
| lux_config      | Configuration engine                  | include/config_engine.h            | Phase 1     |
| lux_web         | Web server and frontend               | include/web_server.h               | Phase 1     |
| lux_log         | Structured logging system             | include/logger.h                   | Phase 1     |
| lux_core        | DMX/RDM core logic                    | include/core/*                     | Phase 2     |
| lux_net         | Network protocols                     | include/net/*                      | Phase 2     |
| lux_test        | Test utilities and shims              | include/test/*                     | Phase 1     |

## Build System

- **ESP-IDF version**: v6.0.1 (installed toolchain + framework; REFACTOR_PLAN.md section 3.x assumes v5.2 - see Lessons_Learned. No Phase-1 breakage.)
- **PlatformIO environments**: esp32dev, wt32eth01, esp32s3_psram
- **Build flags**: Core affinity, task priorities, stack sizes
- **Template generation**: Configuration defaults, web assets
- **CI/CD**: GitHub Actions for build, test, and verification

## Key Interfaces

### Hardware Abstraction
```c
// LED Driver Interface
typedef struct {
    esp_err_t (*init)(void);
    esp_err_t (*set_pattern)(led_pattern_t pattern);
    esp_err_t (*set_brightness)(uint8_t brightness);
    esp_err_t (*update)(void);
} led_driver_t;
```

### Configuration Engine
```c
// Configuration Field Descriptor
typedef struct {
    const char* key;
    const char* json_key;
    cfg_type_t type;
    void* value_ptr;
    int min;
    int max;
    const char* label;
    const char* group;
    uint32_t flags;  // CFG_LIVE, CFG_REBOOT, CFG_SECRET, etc.
} cfg_field_t;
```

### WiFi Manager
```c
// WiFi Event Callback
typedef void (*wifi_event_cb_t)(wifi_event_t event, void* data);

esp_err_t wifi_manager_init(wifi_event_cb_t cb);
esp_err_t wifi_sta_connect(const char* ssid, const char* password);
esp_err_t wifi_start_softap(const char* ssid, const char* password);
```

## Current Status

**Phase 1 (WiFi + LED + Config) - build-gate green

Build gate (plan section Constraints / Phase 0 exit #1): `pio run -e {esp32s3_psram,esp32dev,wt32eth01}` all SUCCESS (clean rebuild; ~850 KB firmware.bin). ESP-IDF framework v6.0.1; toolchain cached under ~/.platformio.

Native gate (Phase 0 exit #2 / section 5.4): host-native harness (`test/`, MinGW gcc) builds + `ctest` 4/4 PASS (logger_test, config_serial_test, portal_test, boards_test).

- [x] Project structure + canonical board table (boards.c/boards.h; gap-a closed)
- [x] Hardware abstraction layer (auto-detect + net_if)
- [x] WiFi STA connect-from-config + NET_STATE_* + exponential backoff, GPIO0 forced-portal, SoftAP(SSID=hostname)
- [x] Setup portal: custom lwIP DNS sinkhole (UDP:53 -> 192.168.4.1), `POST /setup` -> NVS -> reboot
- [x] LED: boot + net-state patterns + brightness clamp
- [x] Config engine (9-field Phase-1 schema; NVS overlay; secret masking; board template)
- [x] Serial console subset (dump/get/set/save/reset/load/wifi/reboot)
- [x] Web routes (/info.json, /wifi/scan, /setup GET+POST, /config, /assets) + standalone webui (MockTransport)
- [x] Testing infrastructure (native harness green 6/6, 76 total test cases)
- [x] Net baseline test: portal activation matrix (spec 33), backoff formula (spec 14), WiFi state machine, config persistence
- [x] LED math test: brightness scaling, clamping, pattern-to-color mapping (spec 36)
- [ ] CI/CD pipeline (Phase 0 section 5.6 #13 - pending)

Remaining Phase-1 follow-ups (parity register section 10): `wifi_ssid` CFG_LIVE -> CFG_REBOOT per spec 45: DONE; serial `help`/`factory` verbs (spec 43): DONE; config_engine board-template: already reconciled (hardware fields sourced from boards.c, section 5.2). webui `/wifi/scan` vs plan section 5.7 `/setup/scan` (accepted deviation). See Lessons_Learned.

## References

- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/index.html)
- [PlatformIO ESP-IDF Integration](https://docs.platformio.org/en/latest/frameworks/espidf.html)
- [LuxDMX-v2 Documentation](../docs/)
- [Refactoring Plan](docs/REFACTOR_PLAN.md)
