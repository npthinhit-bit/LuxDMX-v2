# Network State Subsystem — System Specification

Domain: net.net-state

## 1. Module Overview

The Network State subsystem is the network interface abstraction layer. It provides uniform accessors that work across WiFi station, WiFi access-point, and wired Ethernet modes, and centralizes all network-mode global state flags. It owns the WiFi connection orchestration: strongest-AP roaming, static IP or DHCP application, deterministic link-local (AutoIP) fallback on DHCP failure, MAC-based deterministic AutoIP address selection, and the setup-portal activation path triggered by missing credentials, failed connection, or a held BOOT button.

**Owns:** Global network mode flags, WiFi connection orchestration, static IP configuration, AutoIP link-local fallback, MAC-based deterministic AutoIP selection, WiFi-to-Ethernet credential migration.
**Delegates to:** ethernet bring-up, setup-portal (captive DNS), LED status (connection indication), syslog (connect notification).
**Consumed by:** system bring-up (setup phase 4), ArtNet bridge (PollReply IP/MAC and source validation), web server (captive DNS redirect), network-reception task (core 0).

## 2. External Interfaces

### 2.1 Global Mode Flags

| Flag | Type | Description |
|---|---|---|
| g_apMode | bool | WiFi is in access-point (standalone) mode |
| g_useEth | bool | Wired Ethernet is the active interface |
| g_apWiredFallback | bool | AP started as a wired link-loss fallback |
| g_ethFallback | bool | WiFi fallback activated after wired link loss |
| g_setupPortal | bool | Setup portal (captive DNS) is active |

### 2.2 Accessor Interfaces

| Accessor | Returns | Consumer |
|---|---|---|
| netConnected() | bool | System bring-up link check |
| netLocalIP() | IPAddress | ArtNet PollReply IP field |
| netSubnetMask() | IPAddress | ArtNet PollReply field |
| netGatewayIP() | IPAddress | ArtNet PollReply field |
| netSSID() | String | ArtNet PollReply field |
| netRSSI() | int8 | Status reporting |
| netIsLocalSubnet(ip) | bool | ArtNet IP-Prog source validation |

### 2.3 Configuration Fields

| Field | Apply Semantics | Description |
|---|---|---|
| hostname | live | WiFi hostname, mDNS registration, AP SSID |
| wifiMode | reboot | Station (0) vs access-point (1) |
| wifiSsid | secret | WiFi network credentials |
| wifiPsk | secret | WiFi password |
| staticIp | reboot | Static vs DHCP addressing |
| ip, gateway, subnet, dns | reboot | Static IP configuration |
| autoIpFallback | live | Enable 169.254.x.x link-local on DHCP failure |
| apPassword | secret | Access-point WPA2 password |

## 3. State Machine

### 3.1 WiFi / Interface Mode

```
[Boot]
  |
  +-- cfg.useEthernet=true --> [Wired Ethernet] (ethernet bring-up)
  |
  +-- cfg.wifiMode == AP --> [WiFi AP mode] (g_apMode = true)
  |
  +-- [WiFi Station mode]
        |
        +-- WiFi.begin() --> [Connecting]
              |
              +-- WL_CONNECTED --> [Connected]
              |
              +-- failure + autoIpFallback --> [AutoIP 169.254.x.x]
              |
              +-- failure + no AutoIP --> [Setup Portal] (g_setupPortal = true)
```

### 3.2 WiFi Station Connection States

| State | Entry Condition | Exit Condition |
|---|---|---|
| Initial | startWiFiStation called | BOOT-held or no SSID ? Setup Portal |
| BOOT held | GPIO0 LOW for up to 3 s | Button released ? normal WiFi; still held ? Setup Portal |
| Connecting | WiFi.begin() issued | WL_CONNECTED or 30 s timeout |
| Connected | WL_CONNECTED achieved | Steady state |
| AutoIP fallback | DHCP fail + autoIpFallback enabled | After AutoIP probe, retry DHCP |
| Setup Portal | No credentials, BOOT held, or connect fail | User configures via web UI |

## 4. Data Flow

### 4.1 WiFi Station Bring-Up

1. The BOOT button (GPIO0) is sampled; if held LOW for up to 3 seconds, the setup portal is forced.
2. If a static IP is configured without WiFi credentials, the static configuration is cleared.
3. WiFi is set to station mode and the configured hostname is applied.
4. If no SSID is present, legacy WiFi credentials are migrated from NVS (from a prior WiFiManager setup).
5. If there are no credentials or the portal is forced, the setup portal is started.
6. Static IP configuration is applied (static values or DHCP).
7. `WiFi.begin()` is called with the SSID and PSK; the connection attempt is given a 30-second window with the boot-connection LED active.
8. On failure with AutoIP enabled, a deterministic link-local address `169.254.<third>.<fourth>` is selected based on the MAC address.
9. On failure without AutoIP (or after AutoIP retry fails), the setup portal is started.
10. On success, the strongest-AP roaming function is called, WiFi power-save is explicitly disabled, and the connection is logged and announced via syslog.

### 4.2 Setup Portal Activation

1. The setup portal starts a WiFi access point (open, no password) using the configured hostname as the SSID.
2. A captive DNS server is started, redirecting all queries to the access-point IP.
3. The `g_setupPortal` flag is set, causing the main loop to pump DNS requests.
4. The web server's not-found handler redirects unmatched routes to `/` when the portal is active.

## 5. Configuration Integration

The subsystem reads WiFi credentials, static IP settings, hostname, AutoIP fallback enable, and AP password from the config engine. Secret fields (credentials, AP password) are handled as masked values. The hostname is applied live to WiFi, mDNS, and the AP SSID. Static IP and WiFi mode are reboot-apply settings. AutoIP fallback is live-apply, allowing runtime toggling of link-local behavior.

## 6. Lifecycle

1. **Setup phase 4:** Wifi AP, WiFi station, or wired Ethernet bring-up is initiated based on configuration.
2. **mDNS (setup):** mDNS services are registered after the network is up.
3. **Main loop:** The setup-portal DNS pump runs when the portal flag is active.
4. **netRxTask (core 0):** Network packet processing task consumes the network state accessors.

## 7. Error Handling

| Condition | Behavior |
|---|---|
| BOOT button held (GPIO0 LOW) | Forces the setup portal |
| No WiFi credentials | Starts the setup portal |
| WiFi connect timeout (30 s) | AutoIP fallback if enabled, otherwise setup portal |
| AutoIP DHCP retry fails (10 s) | Stays on the link-local address |
| AP password shorter than 8 characters | Refuses to start the AP when a password is required |
| Non-local-subnet ArtIpProg request | Logged and ignored |

## 8. Timing Constraints

| Operation | Budget |
|---|---|
| WiFi connection attempt | 30 seconds |
| BOOT button hold detection | 3 seconds |
| AutoIP DHCP retry | 10 seconds |
| Strongest-AP reconnect scan | 12 seconds |

## 9. Memory and Allocation Model

All global mode flags are static booleans stored in BSS. The captive DNS server is a static global instance with static allocation. The WiFi connection window and strongest-AP scanning use stack-allocated IP address objects. No heap allocation occurs on the normal network bring-up path.

## 10. Safety and Reliability

WiFi power-save is explicitly disabled (`setSleep(WIFI_PS_NONE)`) after a successful connection. This prevents WiFi power-save polling from introducing latency into the 2 ms network-reception task loop, which is critical for deterministic ArtNet and sACN packet processing. The 30-second connect timeout blocks the setup sequence; there is no asynchronous reconnect path visible, meaning a persistent network failure stalls setup until either AutoIP or the setup portal engages. The setup portal provides a fail-open recovery path: if no credentials exist or connection fails, the device becomes a configurable access point so the user can provision WiFi via the web UI. The AutoIP fallback preserves partial network connectivity (link-local) even when the DHCP server is unavailable.

## 11. Concurrency Model

All functions are single-threaded on core 0, called from `setup()` before tasks are spawned, or from the main loop and the network-reception task on core 0. No cross-core access to the global mode flags occurs. The only cross-task interaction is the setup-portal DNS pump in the main loop.

## 12. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| net.ethernet | downstream | Wired Ethernet bring-up, called from setup |
| net.setup-portal | downstream | Captive DNS portal started on connect failure |
| sys.led-status | downstream | Boot connection LED during WiFi connect |
| sys.syslog | downstream | Syslog notification on successful WiFi connect |
| net.artnet-protocol | upstream | Uses netLocalIP, netSSID for PollReply |
| net.artnet-bridge | upstream | Uses netIsLocalSubnet for ArtIpProg source validation |
| sys.tasks | upstream | netRxTask consumes network state on core 0 |

## 13. Testing Verification

No dedicated host-native unit test covers the network state abstractions. WiFi and Ethernet bring-up paths are validated through hardware integration testing. No native host test covers the interface mode selection, AutoIP deterministic selection, or the setup-portal activation logic.

## 14. Open Questions

1. Whether WiFi disconnection and roaming is handled after the initial connect — no event handler is visible in the inspected source, only the initial connect path in `startWiFiStation`.
2. Whether the DHCP failure detection is accurate — `WiFi.begin()` returns before DHCP completes, and the `WL_CONNECTED` check may reflect association rather than successful address assignment.

