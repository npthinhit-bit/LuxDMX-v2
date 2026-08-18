# Hardware Tiers — Compatibility and Configuration Guide

The goal of this guide is to help you derive the right configuration for *any* hardware, not just a fixed list of known boards. Use Tier 1 boards as reference patterns you can adapt.

---

## How to Find Your Board Configuration

### Step 1: Find the Zephyr board name

```bash
west boards | grep <keyword>   # e.g., "nrf91", "esp32", "frdm"
```

This gives you the exact string to use with `-b`. For example:
- `nrf9160dk/nrf9160/ns` for the nRF9160 DK
- `nrf9151dk/nrf9151/ns` for the nRF9151 DK (same NCS pattern as 9160)
- `esp32s3_devkitc/esp32s3/procpu` for ESP32-S3 DevKitC
- `nrf52840dk/nrf52840` for nRF52840 DK

### Step 2: Find the network interface type

What connects this device to the internet?
- **Cellular modem (integrated)**: nRF9160/9151, SiP modules — modem is on-chip, no AT modem config needed
- **WiFi (integrated)**: ESP32 family, FRDM-RW612, CYW43xxx — use Zephyr WiFi credentials API
- **AT modem (external, UART)**: e.g., ESP32 acting as modem for nRF52840 — needs shield overlay
- **Ethernet**: use Zephyr Ethernet driver, configure net settings
- **LoRa/LTE Cat-M via external modem**: RAK5010/BG96 pattern

The network interface type determines your overlay and `prj.conf` content.

### Step 3: Find the closest Golioth SDK sample for your SoC family

Look in the SDK's `examples/zephyr/<sample>/boards/` directory. Each sample ships with overlay files for tested hardware. If your board isn't there, find the closest SoC family:

- nRF9151 → look at nRF9160 overlays, they are compatible (same NCS modem pattern)
- nRF5340 → look at nRF52840 overlays as a starting point, adjust for dual-core
- ESP32-C3/C6 → look at ESP32-S3 overlays, same WiFi driver family
- Any custom nRF SiP → start from the corresponding DK overlay

The overlay pattern is: set up the network interface, configure credentials storage (NVS or settings), and ensure flash partitions are correct for OTA.

---

## Tier Definitions

### Tier 1 — CI-Tested (Known-Good)

These SoC families and their common dev kits are tested on hardware-in-the-loop runners on every Golioth SDK commit. They are the safest starting point and the best source of reference overlays.

**Nordic Semiconductor (NCS):**
- nRF9160, nRF9151 family — integrated LTE-M/NB-IoT modem
- nRF52840, nRF5340 — with external AT modem (e.g., ESP32 over UART)
- Use NCS (`nrf` west workspace), not vanilla Zephyr

**Espressif (Zephyr and ESP-IDF):**
- ESP32-S3, ESP32-S2, ESP32 family — integrated WiFi
- Both Zephyr and native ESP-IDF builds are tested
- OTA mechanism differs between the two (see below)

**NXP:**
- FRDM-RW612 and RW612 family — integrated WiFi

**Infineon (ModusToolbox):**
- CYW43xxx / PSoC6 family
- Uses ModusToolbox toolchain, not west

For any of these SoC families, even on a custom board or a newer DK variant, follow the pattern from the closest reference design. The Zephyr board name will differ but the configuration structure is the same.

### Tier 2 — Zephyr-Supported with Network

Any board in the Zephyr board catalog with a working network interface (WiFi, cellular, Ethernet). To get working:
1. Run `west boards` to confirm your board is listed
2. Find the closest Tier 1 overlay in the SDK samples
3. Adapt pin assignments (UART RX/TX, SPI CS, etc.) for your specific board
4. `prj.conf` Kconfig requirements are identical regardless of board

### Tier 3 — Network Interface Needs Work

Board is Zephyr-supported but the network layer isn't verified yet. Do not begin Golioth integration until you can independently confirm connectivity (e.g., send an HTTP request, receive a CoAP ping response). Refer to [zephyr-agent-skills/connectivity-ip](https://github.com/beriberikix/zephyr-agent-skills) for Zephyr networking setup.

### Tier 4 — Custom Hardware

Custom PCB. Zephyr bringup may be needed first. Once Zephyr boots and the network interface works, Golioth integration is identical to Tier 2. The SDK doesn't care about hardware specifics once the network socket layer is functional.

---

## Platform-Specific OTA Notes

OTA mechanism depends on the platform, not the board. Get this right early.

| Platform | OTA mechanism | MCUboot? |
|----------|--------------|----------|
| Zephyr (non-ESP) | MCUboot via sysbuild | Yes |
| NCS (nRF Connect SDK) | MCUboot via sysbuild | Yes |
| ESP-IDF | esp_ota_ops + OTA partitions | No |
| Zephyr on ESP32 | MCUboot via sysbuild | Yes (different from ESP-IDF!) |
| ModusToolbox | ModusToolbox DFU | No |

The same ESP32-S3 chip can use MCUboot (Zephyr build) or esp_ota_ops (ESP-IDF build) — the platform determines which path, not the silicon.

---

## Build Command Pattern

The general pattern for any Zephyr board with OTA:

```bash
# With sysbuild (required for MCUboot OTA on Zephyr/NCS)
west build -b <board-name> --sysbuild

# Without sysbuild (no OTA, or ESP-IDF path)
west build -b <board-name>

# With extra config overlay
west build -b <board-name> --sysbuild -- -DEXTRA_CONF_FILE=overlay-foo.conf
```

## Credential Setup (Zephyr shell — all boards)

```
uart:~$ settings set golioth/psk-id <device-id@project-name>
uart:~$ settings set golioth/psk <psk-secret>
# WiFi boards only (v0.21+ uses Zephyr WiFi Credentials):
uart:~$ wifi_cred add "<ssid>" WPA2-PSK "<password>"
uart:~$ kernel reboot cold
```

Credentials are stored in NVS/flash settings partition and persist across reboots. Never hardcode them in source (removed from SDK in v0.18.0).
