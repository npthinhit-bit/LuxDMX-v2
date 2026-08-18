# LuxDMX-v2 ESP-IDF Codebase Index

## Project Overview

LuxDMX-v2 is an Art-Net/sACN → DMX512 gateway firmware for ESP32/ESP32-S3 with full RDM support, OTA updates, and web configuration interface. This version is a **pure ESP-IDF implementation** with PlatformIO integration, replacing the previous hybrid Arduino/ESP-IDF approach.

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
├── lux_common/      # Common utilities and base classes
├── lux_hw/          # Hardware abstraction layer
├── lux_wifi/        # WiFi and network management
├── lux_led/         # LED drivers and status indication
├── lux_config/      # Configuration engine
├── lux_core/        # DMX/RDM core logic (Phase 2+)
├── lux_net/         # Network protocols (Phase 2+)
├── lux_web/         # Web server and frontend
├── lux_log/         # Structured logging
└── lux_test/        # Test utilities and shims
```

## Key Architectural Decisions

### 1. Core Affinity Strategy
- **Core 0**: Network stack, WiFi, AsyncTCP, web server, serial console
- **Core 1**: DMX transmit, RDM service, merge engine (time-critical operations)
- **Rationale**: Prevent network activity from preempting time-critical DMX/RDM operations

### 2. Configuration System
- **Schema-driven**: Single field table drives NVS, web form, and serial console
- **Resolution order**: Neutral values → board template → NVS overlay
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

- **ESP-IDF version**: v5.2
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

**Phase 1 (WiFi + LED + Config) In Progress**
- [x] Project structure setup
- [x] Hardware abstraction layer
- [x] WiFi implementation (station + SoftAP)
- [x] LED driver implementation
- [x] Configuration engine
- [x] Web interface foundation
- [ ] Testing infrastructure
- [ ] CI/CD pipeline

## References

- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/index.html)
- [PlatformIO ESP-IDF Integration](https://docs.platformio.org/en/latest/frameworks/espidf.html)
- [LuxDMX-v2 Documentation](../docs/)
- [Refactoring Plan](docs/REFACTOR_PLAN.md)