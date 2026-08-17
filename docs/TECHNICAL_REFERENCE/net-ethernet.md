Ethernet — Technical Reference

Domain: `net.ethernet`

## 1. Domain Scope

Ethernet bring-up for wired boards: W5500 SPI-Ethernet and RMII LAN8720. Provides `startWiredEth()` (runtime-selected between SPI and RMII paths via `cfg.wiredPhy`), `applyEthStaticIp()`, `waitEthLink()`, and `applyWiredLinkLoss()`. The W5500 driver is compiled in when `HAS_ETH_SPI` is defined; the LAN8720 RMII driver is compiled when `HAS_ETH_RMII` is defined.

**Owns:** Ethernet interface initialization, static IP configuration, link-loss fallback policy, RMII PHY family name table + type mapping.
**Delegates to:** `ETH.h` (Arduino-ESP32 Ethernet API), `eth_phy.h` (RMII PHY enum → `eth_phy_type_t` mapping).
**Consumed by:** `main.cpp:64-69` (setup), `net_state.cpp` (link-loss fallback).

## 2. Layer Mapping

| Layer | Directory | Role |
|---|---|---|
| net | `src/net/` | Wired Ethernet interface bring-up |
| include | `include/` | RMII PHY family lookup (`eth_phy.h`) |

## 3. Source Files

| File | Role |
|---|---|
| `src/net/ethernet.h` | Function declarations, compile-time SPI pin defaults, wired PHY select macros |
| `src/net/ethernet.cpp` | `waitEthLink`, `applyEthStaticIp`, `w5500HardReset`, `ethSpiUpTask`, `startEthSpi`, `rmiiClkMode`, `ethRmiiUpTask`, `startEthRmii`, `startWiredEth`, `applyWiredLinkLoss` |
| `include/eth_phy.h` | `RMII_PHY_NAMES[]` table, `rmiiPhyType()` mapping function |
| `src/net/net_state.h` | Network state globals (`g_useEth`, `g_apMode`, etc.) |
| `src/main.cpp` | Setup wiring: `startWiredEth()`, `applyWiredLinkLoss()` |

## 4. Data Structures

### Link-Loss Fallback Policy (config_enums.h:9)

| Enum | Value | Description |
|---|---|---|
| `WIRED_FB_RETRY` | 0 | Keep retrying the wired link (default) |
| `WIRED_FB_AP` | 1 | Open a WPA2 WiFi AP for configuration |
| `WIRED_FB_REBOOT` | 2 | Reboot on link loss |
| `WIRED_FB_WIFI` | 3 | Fall back to WiFi STA |

### Wired PHY Selection (ethernet.h:45-48)

| Macro | Value | Description |
|---|---|---|
| `WIRED_PHY_SPI` | 0 | W5500/DM9051 SPI Ethernet |
| `WIRED_PHY_RMII` | 1 | LAN8720/integrated EMAC RMII |

### SPI PHY Selection (ethernet.h:47-48)

| Macro | Value | Description |
|---|---|---|
| `ETH_SPI_PHY_W5500` | 0 | W5500 chip |
| `ETH_SPI_PHY_DM9051` | 1 | DM9051 chip |

### Compile-Time SPI Defaults (ethernet.h:14-42)

| Macro | Default | Description |
|---|---|---|
| `ETH_W5500_SCK` | 18 | W5500 SPI clock pin |
| `ETH_W5500_MOSI` | 23 | W5500 SPI MOSI pin |
| `ETH_W5500_MISO` | 19 | W5500 SPI MISO pin |
| `ETH_W5500_CS` | 5 | W5500 SPI CS pin |
| `ETH_W5500_IRQ` | 4 | W5500 SPI IRQ pin |
| `ETH_W5500_RST` | 25 | W5500 SPI RST pin |
| `ETH_W5500_SPI_FREQ_MHZ` | 20 | SPI frequency |
| `ETH_W5500_SPI_HOST` | SPI3_HOST | SPI host |
| `ETH_W5500_ADDR` | 1 | W5500 SPI address |

## 5. Concurrency

Bring-up runs on core 0. `startEthSpi` and `startEthRmii` each spawn a FreeRTOS task pinned to core 0 (`xTaskCreatePinnedToCore(..., 0)` at `ethernet.cpp:65,96`) with priority 5 and stack 8192. This isolates EMAC/SPI interrupts from core 1 (where DMX/RDM TX runs).

The `s_ethUpDone` / `s_ethRmiiUpDone` flags are `volatile bool` and polled by the caller with a 30-second timeout (`ethernet.cpp:67,98`).

## 6. State Machine

### Wired Ethernet Bring-Up

```
[Disabled] --cfg.useEthernet=true--> [Starting SPI/RMII task]
                                      ↓
                   [ETH.begin() + waitEthLink()] --link up--> [Up]
                                      ↓
                           --link down--> [Link Loss Policy]
```

### Link-Loss Fallback (`applyWiredLinkLoss`)

| `linkLossMode` | `atBoot=true` | `atBoot=false` |
|---|---|---|
| `WIRED_FB_RETRY` (0) | Do nothing | Do nothing |
| `WIRED_FB_AP` (1) | Start WiFi AP (`g_apWiredFallback=true`) | Start WiFi AP |
| `WIRED_FB_REBOOT` (2) | Do nothing (avoids boot loop) | Reboot after 200 ms |
| `WIRED_FB_WIFI` (3) | Switch to WiFi STA (`g_useEth=false`) | Reboot after 200 ms |

## 7. Entry Points

| Function | Called from | Purpose |
|---|---|---|
| `startWiredEth()` | `main.cpp:64` | Top-level: dispatch to SPI or RMII path |
| `startEthSpi()` | `startWiredEth` (`ethernet.cpp:103-118`) | W5500 bring-up via core-0 task |
| `startEthRmii()` | `startWiredEth` (`ethernet.cpp:103-118`) | LAN8720 bring-up via core-0 task |
| `applyWiredLinkLoss(true)` | `main.cpp:67` | Apply link-loss policy at boot |
| `applyEthStaticIp()` | `ethSpiUpTask`/`ethRmiiUpTask` (`ethernet.cpp:54,86`) | Configure static or DHCP IP |
| `waitEthLink()` | `ethSpiUpTask`/`ethRmiiUpTask` (`ethernet.cpp:55,87`) | Block until link up or 15 s timeout |

## 8. Data Flow

### SPI (W5500) Path

1. `startWiredEth()` checks `cfg.wiredPhy` to dispatch to SPI or RMII: `ethernet.cpp:103-118`
2. `startEthSpi()` logs pin config, spawns `ethSpiUpTask` on core 0: `ethernet.cpp:60-69`
3. `ethSpiUpTask` hard-resets the W5500 (if `ethRst >= 0`), calls `ETH.begin()` with W5500 params: `ethernet.cpp:40-58`
4. `applyEthStaticIp()` configures IP/DHCP: `ethernet.cpp:20-26`
5. `waitEthLink()` blocks up to 15 s for `ETH.linkUp()`: `ethernet.cpp:11-18`
6. Caller polls `s_ethUpDone` with 30 s timeout: `ethernet.cpp:67`

### RMII (LAN8720) Path

1. `startEthRmii()` logs PHY config, spawns `ethRmiiUpTask` on core 0: `ethernet.cpp:91-100`
2. `ethRmiiUpTask` calls `ETH.begin()` with RMII PHY type, address, MDC, MDIO, power, clock mode: `ethernet.cpp:81-90`
3. Same static IP + link wait flow as SPI path

### Link-Loss Policy

1. At boot (`atBoot=true`): `applyWiredLinkLoss(true)` is called if `!netConnected()` after `startWiredEth()`: `main.cpp:65-68`
2. At runtime: link state changes are detected in `netConnected()` (`net_state.cpp:18-24`), but the runtime link-loss trigger is not determinable from the inspected source code.

## 9. Protocol Layout

N/A (no wire protocol — Ethernet is handled by the Arduino-ESP32 `ETH.h` abstraction layer).

## 10. Configuration Integration

| Config Field | Source | Usage | Flags |
|---|---|---|---|
| `useEthernet` | `config_schema.cpp:103` | Gate for wired path: `main.cpp:59,63` | `CFG_REBOOT` |
| `wiredPhy` | `config_schema.cpp:96` | Selects SPI (0) vs RMII (1): `ethernet.cpp:112-113` | `CFG_REBOOT` |
| `ethW5500` | `config_schema.cpp:87` | Secondary gate for W5500: `main.cpp:61` | `CFG_NONE` |
| `ethSpiPhy` | `config_schema.cpp:95` | W5500 vs DM9051: `ethernet.cpp:42,44` | `CFG_REBOOT` |
| `ethCs`, `ethSck`, etc. | `config_schema.cpp:88-94` | SPI pin config: `ethernet.cpp:49,63` | `CFG_REBOOT` |
| `ethFreqMhz` | `config_schema.cpp:94` | SPI clock: `ethernet.cpp:49` | `CFG_REBOOT` |
| `rmiiPhy` | `config_schema.cpp:97` | RMII PHY family: `ethernet.cpp:82-83,92` | `CFG_REBOOT` |
| `rmiiAddr` | `config_schema.cpp:98` | RMII SMI address: `ethernet.cpp:83,92` | `CFG_REBOOT` |
| `rmiiMdc` | `config_schema.cpp:99` | RMII MDC pin: `ethernet.cpp:83,92` | `CFG_REBOOT` |
| `rmiiMdio` | `config_schema.cpp:100` | RMII MDIO pin: `ethernet.cpp:83,92` | `CFG_REBOOT` |
| `rmiiPwr` | `config_schema.cpp:101` | RMII PHY power pin: `ethernet.cpp:83,92` | `CFG_REBOOT` |
| `rmiiClk` | `config_schema.cpp:102` | RMII clock mode: `ethernet.cpp:73-79` | `CFG_REBOOT` |
| `staticIp` | `config_schema.cpp:109` | Gate for static IP: `ethernet.cpp:21` | `CFG_REBOOT` |
| `ip`, `gateway`, `subnet`, `dns` | `config_schema.cpp:110-113` | Static IP config: `ethernet.cpp:22-25` | `CFG_REBOOT` |
| `hostname` | `config_schema.cpp:47` | mDNS hostname: `ethernet.cpp:53,85` | `CFG_LIVE` |
| `linkLossMode` | `config_schema.cpp:107` | Link-loss policy: `ethernet.cpp:121-143` | `CFG_REBOOT` |
| `vlanEnabled`, `vlanId` | `config_schema.cpp:118-119` | VLAN tag (unsupported): `ethernet.cpp:105-109` | `CFG_REBOOT` |

## 11. Lifecycle

1. **Init (setup):** `main.cpp:58-70` — checks `HAS_WIRED_ETH`, gates on `cfg.useEthernet` and `cfg.wiredPhy`/`cfg.ethW5500`, calls `startWiredEth()`
2. **Link check (setup):** If `!netConnected()` after bring-up, `applyWiredLinkLoss(true)` at `main.cpp:67`
3. **No deinit** — Ethernet persists for device lifetime.

## 12. Error Handling

| Condition | Handling | Source |
|---|---|---|
| VLAN tagging requested | Logs unsupported, continues with untagged frames | `ethernet.cpp:105-109` |
| W5500 without RST pin (`ethRst < 0`) | Skips hard reset | `ethernet.cpp:29` |
| DM9051 chip selected but not in build | Logs, falls back to W5500 | `ethernet.cpp:44-45` |
| SPI bring-up timeout (30 s) | Logs "still running" | `ethernet.cpp:68` |
| RMII bring-up timeout (30 s) | Logs "still running" | `ethernet.cpp:99` |
| W5500 init failure | Logs error, returns false from `rmtDmxInit` equivalent path | `ethernet.cpp` |

## 13. Memory Allocation

- `s_ethUpDone`, `s_ethRmiiUpDone` — static `volatile bool` (ethernet.cpp:8-9)
- Core-0 bring-up tasks: 8192-byte stack each, pinned to core 0 (ethernet.cpp:65,96)
- No heap allocation; all Ethernet state is managed by the Arduino-ESP32 `ETH` singleton.

## 14. Timing

| Operation | Budget | Source |
|---|---|---|
| Link-up wait | 15 000 ms | `ethernet.cpp:13` |
| SPI/RMII bring-up wait | 30 000 ms | `ethernet.cpp:67,98` |
| W5500 hard reset | 600 µs + 2 ms | `ethernet.cpp:32-34` |

Both ESP32 and ESP32-S3 builds run Ethernet bring-up on core 0 (priority 5), isolated from the core-1 DMX task. VLAN tagging (802.1Q) is NOT supported by the Arduino W5500 driver — users needing VLAN must use the ESP-IDF native build.

## 15. Traceability / Evidence

| Claim | Source |
|---|---|
| `startWiredEth` selects SPI vs RMII by `cfg.wiredPhy` | `ethernet.cpp:103-118` |
| SPI path hard-resets W5500 if `ethRst >= 0` | `ethernet.cpp:28-36` |
| `ethSpiUpTask` pinned to core 0, prio 5 | `ethernet.cpp:65` |
| RMII path uses `rmiiPhyType()` mapping | `ethernet.cpp:82-83` |
| 30 s bring-up timeout (caller polls) | `ethernet.cpp:67,98` |
| `applyWiredLinkLoss` policy table | `ethernet.cpp:121-143` |
| `atBoot=true` never reboots (avoids boot loop) | `ethernet.cpp:128,137` |
| VLAN tagging unsupported, logged | `ethernet.cpp:105-109` |
| Static IP via `ETH.config()` or `WiFi.config()` | `ethernet.cpp:20-26` |
| Called from main.cpp setup phase 4 | `main.cpp:64-69` |
| `WIRED_PHY_SPI`/`WIRED_PHY_RMII` macros | `ethernet.h:45-46` |
| RMII PHY names table | `eth_phy.h:7-9` |

## 16. Cross-References

- [Net State](./net-net-state.md) — network interface abstraction, `netConnected()`, link-loss integration
- [Setup Portal](./net-setup-portal.md) — captive DNS portal started on link loss
- [LED Status](./sys-led-status.md) — `bootConnectingLed()` called during WiFi fallback
- [Art-Net Protocol](./net-artnet-protocol.md) — ArtNet socket initialized after Ethernet is up
- [Include Headers](./include-headers.md) — `eth_phy.h` RMII PHY mapping

## 17. Limitations

- VLAND tagging (802.1Q) is not supported by the Arduino W5500 driver — users needing VLAN must use ESP-IDF native build (`ethernet.cpp:105-109`).
- Only one DM9051 support path: if `ETH_SPI_PHY_DM9051` is selected but `CONFIG_ETH_SPI_ETHERNET_DM9051` is not defined, the chip silently falls back to W5500 (`ethernet.cpp:42-45`).
- The runtime link-loss trigger (when `atBoot=false`) is not determinable from the inspected source code — `applyWiredLinkLoss` is only called at boot from `main.cpp:67`.

## 18. Open Questions

- Not determinable from the inspected source code — where `applyWiredLinkLoss(false)` is called at runtime for link-loss detection.
- Not determinable from the inspected source code — the DHCP retry/failure behavior inside `ETH.begin()` (handled by the Arduino core).

## 19. Testing

No dedicated unit test for `ethernet.cpp`. Ethernet bring-up is validated through hardware integration testing on wired boards (WT32-ETH01, LuxDMX v6, LuxDMX-4uni). No native host test covers the SPI/RMII initialization paths.

## 20. History

No recorded changes.
