# Ethernet Subsystem — System Specification

Domain: net.ethernet

## 1. Module Overview

The Ethernet subsystem manages bring-up of wired network interfaces for supported boards, supporting both W5500 SPI-Ethernet and LAN8720/integrated EMAC RMII PHY families. At runtime it selects the PHY transport (SPI vs RMII) based on the `wiredPhy` configuration field, initializes the chosen hardware path, optionally applies a static IP configuration, waits for carrier link-up, and enforces a configurable link-loss fallback policy. The W5500 driver is compiled in when the SPI-Ethernet build flag is defined; the LAN8720 RMII driver is compiled in when the RMII build flag is defined.

**Owns:** Ethernet interface initialization, static IP configuration, link-loss fallback policy, RMII PHY family name table and type mapping.
**Delegates to:** Arduino-ESP32 Ethernet API, RMII PHY enum-to-type mapping layer.
**Consumed by:** system bring-up (setup phase 4), network state abstraction (link-loss integration).

## 2. External Interfaces

### 2.1 Configuration Fields

| Field | Apply Semantics | Description |
|---|---|---|
| useEthernet | reboot | Gate for the wired path |
| wiredPhy | reboot | Selects SPI (0) vs RMII (1) transport |
| ethW5500 | none | Secondary gate for W5500 presence |
| ethSpiPhy | reboot | W5500 vs DM9051 chip selection |
| ethCs, ethSck, ethMosi, ethMiso, ethRst, ethIrq | reboot | SPI bus pin configuration |
| ethFreqMhz | reboot | SPI bus clock frequency |
| rmiiPhy, rmiiAddr, rmiiMdc, rmiiMdio, rmiiPwr, rmiiClk | reboot | RMII SMI bus and PHY control |
| staticIp | reboot | Gate for static IP (true = static, false = DHCP) |
| ip, gateway, subnet, dns | reboot | Static IP configuration values |
| hostname | live | mDNS hostname; applied to the Ethernet interface |

### 2.2 Link-Loss Fallback Policy Enum

| Value | Name | Description |
|---|---|---|
| 0 | WIRED_FB_RETRY | Keep retrying the wired link (default) |
| 1 | WIRED_FB_AP | Open a WPA2 WiFi access point for configuration |
| 2 | WIRED_FB_REBOOT | Reboot on link loss |
| 3 | WIRED_FB_WIFI | Fall back to WiFi station mode |

### 2.3 PHY Transport Selection

| Value | Name | Description |
|---|---|---|
| 0 | WIRED_PHY_SPI | W5500 or DM9051 SPI Ethernet |
| 1 | WIRED_PHY_RMII | LAN8720 or integrated EMAC RMII |

## 3. State Machine

### 3.1 Wired Ethernet Bring-Up

```
[Disabled] -- useEthernet=true --> [SPI/RMII task starting]
                                      |
                    ETH.begin() + link wait
                                      |
            -- link up --> [Up] -- link down --> [Link Loss Policy]
```

The bring-up flow dispatches on the `wiredPhy` configuration value. The SPI path hard-resets the W5500 chip (when a reset pin is configured), calls `ETH.begin()` with W5500 SPI parameters, applies static IP or DHCP, and waits for link-up. The RMII path calls `ETH.begin()` with the RMII PHY type, SMI address, MDC, MDIO, power pin, and clock mode, then follows the same static IP and link-wait flow.

### 3.2 Link-Loss Fallback Policy

| linkLossMode | at boot | runtime |
|---|---|---|
| WIRED_FB_RETRY | Do nothing | Do nothing |
| WIRED_FB_AP | Start WiFi AP (wired-fallback flag) | Start WiFi AP |
| WIRED_FB_REBOOT | Do nothing (prevents boot loop) | Reboot after 200 ms |
| WIRED_FB_WIFI | Switch to WiFi station (wired disable) | Reboot after 200 ms |

At boot, the link-loss policy is evaluated once after bring-up if the network is not connected. A link-loss mode of REBOOT or WIFI at boot never triggers a reboot to avoid lock-up boot loops; instead the system stays in wired-retry mode.

## 4. Data Flow

### 4.1 SPI (W5500) Bring-Up Flow

1. The `wiredPhy` configuration value is checked to dispatch between the SPI and RMII paths.
2. The SPI bring-up logs the configured pin set and spawns a dedicated bring-up task on core 0.
3. The bring-up task hard-resets the W5500 chip (if a reset pin is configured), then calls `ETH.begin()` with the configured W5500 SPI parameters.
4. Static IP configuration is applied if `staticIp` is set; otherwise DHCP is started.
5. The link-up wait blocks until `ETH.linkUp()` returns true or the link-up timeout expires.
6. The caller polls a completion flag with the full bring-up timeout.

### 4.2 RMII (LAN8720) Bring-Up Flow

1. The RMII bring-up logs the PHY configuration and spawns a dedicated bring-up task on core 0.
2. The bring-up task calls `ETH.begin()` with the RMII PHY type, SMI address, MDC, MDIO, power pin, and clock mode.
3. The same static IP and link-wait flow as the SPI path is followed.

### 4.3 Static IP / DHCP Selection

When `staticIp` is true, the configured IP, gateway, subnet, and DNS values are applied via the Ethernet API's configuration method. When false, DHCP is used and the address is obtained automatically.

## 5. Configuration Integration

The subsystem reads all network-addressing and PHY-selection fields from the config engine with reboot-apply semantics for hardware-bound settings and live-apply semantics for the hostname. The `linkLossMode` field drives the post-bring-up fallback decision. VLAN tagging configuration is accepted but produces a log warning and is silently ignored (untagged frames only).

## 6. Lifecycle

1. **Init (setup):** The system checks for the wired-Ethernet build flag, gates on `useEthernet`, dispatches to the SPI or RMII path, and calls the wired bring-up entry point.
2. **Link check (setup):** If the network is not connected after bring-up, the link-loss fallback policy is evaluated.
3. **Runtime:** The Ethernet interface persists for the device lifetime. No explicit deinitialization occurs.

## 7. Error Handling

| Condition | Behavior |
|---|---|
| VLAN tagging requested | Logs unsupported, continues with untagged frames |
| W5500 reset pin not configured | Skips the hardware hard reset |
| DM9051 chip selected but not compiled in | Logs, falls back to W5500 |
| SPI/RMII bring-up timeout | Logs "still running" status |
| W5500 initialization failure | Logs error, bring-up reports failure |

## 8. Timing Constraints

| Operation | Budget |
|---|---|
| Link-up wait | 15 seconds |
| SPI/RMII bring-up wait (caller poll) | 30 seconds |
| W5500 hard reset pulse | 600 microseconds high, then 2 milliseconds low |

## 9. Memory and Allocation Model

All subsystem-owned state is statically allocated. The bring-up completion flags are static volatile booleans. The core-0 bring-up tasks use a fixed 8192-byte stack each. No heap allocation occurs; all Ethernet PHY state is managed by the Arduino-ESP32 Ethernet singleton.

## 10. Safety and Reliability

Bring-up runs on core 0, isolated from core 1 where the DMX and RDM transmit task executes. This ensures that Ethernet EMAC and SPI interrupts, plus the bring-up task's network stack initialization, cannot preempt the time-critical DMX break and mark-bit timing. The 15-second link-up wait and 30-second overall bring-up timeout prevent indefinite blocking of the setup sequence. At-boot link-loss modes REBOOT and WIFI are deliberately suppressed to avoid boot-loop failures on miswired or unplugged hardware; instead the system stays in retry mode, allowing recovery via a configuration change.

## 11. Concurrency Model

The bring-up tasks are FreeRTOS tasks pinned to core 0 at priority 5 with an 8192-byte stack. The completion flags are volatile and polled by the caller with a timeout. There is no cross-core synchronization required because all Ethernet operation is confined to core 0.

## 12. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| net.net-state | upstream | Network interface abstraction, `netConnected()`, link-loss integration |
| net.setup-portal | downstream | Captive DNS portal started on wired link loss (AP fallback mode) |
| sys.led-status | downstream | Boot connection LED indication during WiFi fallback |
| net.artnet-protocol | downstream | ArtNet socket initialized after Ethernet is up |
| include.eth-phy | internal | RMII PHY family lookup and type mapping |

## 13. Testing Verification

No dedicated host-native unit test covers the Ethernet bring-up paths. Wired Ethernet bring-up is validated through hardware integration testing on wired boards (RMII and SPI-SPI variants). No native host test covers the SPI or RMII initialization paths.

## 14. History

No recorded changes.
