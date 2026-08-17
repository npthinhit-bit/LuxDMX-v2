Network State — Technical Reference

Domain: `net.net-state`

## 1. Domain Scope

Network interface abstraction layer. Provides uniform accessors that work across WiFi STA, WiFi AP, and wired Ethernet modes. Centralizes the global state flags (`g_apMode`, `g_useEth`, `g_apWiredFallback`, `g_ethFallback`, `g_setupPortal`) and the WiFi bring-up logic (`startWiFiStation`, `startWiFiAP`, `connectStrongestAP`, `startSetupPortal`, `migrateWifiCredsFromNvs`).

**Owns:** Global network mode flags, WiFi connection orchestration, static IP configuration, AutoIP (link-local) fallback, MAC-based deterministic AutoIP selection, WiFi→Ethernet credential migration.
**Delegates to:** `ethernet.cpp` (wired bring-up), `setup_portal.cpp` (captive DNS portal), `led_status.cpp` (status LED during connection), `syslog.cpp` (syslog on connect).
**Consumed by:** `main.cpp` (setup phases 4), `artnet.cpp` (PollReply IP/MAC), `ethernet.cpp` (link-loss fallback), `web_server.cpp` (captive DNS redirect).

## 2. Layer Mapping

| Layer | Directory | Role |
|---|---|---|
| net | `src/net/` | Network interface abstraction + WiFi/Ethernet bring-up |

## 3. Source Files

| File | Role |
|---|---|
| `src/net/net_state.h` | Global flags, function declarations, `parseIp` helper |
| `src/net/net_state.cpp` | All implementations: `netConnected`, `netLocalIP`, `netSubnetMask`, `netGatewayIP`, `netSSID`, `netRSSI`, `parseIp`, `netIsLocalSubnet`, `applyStaStaticIp`, `migrateWifiCredsFromNvs`, `startWiFiStation`, `connectStrongestAP`, `startWiFiAP` |
| `src/net/ethernet.h` | `startWiredEth`, `applyWiredLinkLoss` declarations |
| `src/net/setup_portal.cpp` | `startSetupPortal` (captive DNS + AP) |
| `src/main.cpp` | Wiring: calls `startWiredEth`, `startWiFiStation`, `startWiFiAP` in setup |
| `src/sys/tasks.cpp` | `netRxTask` context (consumer of network state) |

## 4. Data Structures

### Global State Flags (net_state.h:22-29)

| Variable | Type | Declared at | Description |
|---|---|---|---|
| `g_apMode` | `bool` | net_state.cpp:12 | WiFi is in AP (standalone) mode |
| `g_useEth` | `bool` | net_state.cpp:13 | Wired Ethernet is the active interface |
| `g_apWiredFallback` | `bool` | net_state.cpp:14 | AP started as a wired link-loss fallback |
| `g_ethFallback` | `bool` | net_state.cpp:15 | WiFi fallback after wired link loss |
| `g_setupPortal` | `bool` | net_state.cpp:16 | Setup portal (captive DNS) is active |
| `dnsServer` | `DNSServer` | setup_portal.cpp:6 | Captive DNS redirect for setup portal |

## 5. Concurrency

Single-threaded on core 0. All functions are called from `setup()` (before tasks are spawned) or from `loop()` / `netRxTask` on core 0. No cross-core access to the global flags. The only cross-core interaction is the setup portal DNS pump in `loop()` at `main.cpp:138`.

## 6. State Machine

### WiFi/Interface Mode

```
[Boot]
  ↓
cfg.useEthernet=true? → [Wired Ethernet] (ethernet.cpp)
  ↓
cfg.wifiMode == AP? → [WiFi AP mode] (g_apMode=true)
  ↓
[WiFi Station mode]:
  ↓
WiFi.begin() → [Connecting] → WL_CONNECTED → [Connected]
  ↓ (failure + autoIpFallback)
[AutoIP 169.254.x.x]
  ↓ (failure + no autoIp)
[Setup Portal] (g_setupPortal=true, AP + captive DNS)
```

### WiFi Station Connection States

| State | Entry | Exit |
|---|---|---|
| Initial | `startWiFiStation` called | GPIO0 LOW or no SSID → Setup Portal |
| BOOT held (3s) | `digitalRead(0) == LOW` | Button released → normal WiFi; still held → Setup Portal |
| Connecting | `WiFi.begin()` | `WL_CONNECTED` or 30 s timeout |
| Connected | `WL_CONNECTED` | N/A (steady state) |
| AutoIP fallback | DHCP fail + `cfg.autoIpFallback` | After AutoIP probe, retry DHCP |
| Setup Portal | No creds / BOOT held / connect fail | User configures via web UI |

## 7. Entry Points

| Function | Called from | Purpose |
|---|---|---|
| `startWiFiStation()` | `main.cpp:74` | Join stored WiFi network, strongest AP |
| `startWiFiAP(bool requirePw)` | `main.cpp:72`; `ethernet.cpp:124` | Start standalone AP |
| `startSetupPortal()` | `net_state.cpp:121` | Start AP + captive DNS (first run / BOOT held) |
| `connectStrongestAP()` | `startWiFiStation` (net_state.cpp:177) | Mesh roaming: scan, pick strongest AP for SSID |
| `netConnected()` | `main.cpp:65` | Check if active interface has link |
| `netLocalIP()` / etc. | `artnet_bridge.cpp:63-72` | PollReply IP/subnet/gateway |
| `netIsLocalSubnet(ip)` | `artnet_bridge.cpp:122` | ArtIpProg source validation |
| `applyStaStaticIp()` | `startWiFiStation` (net_state.cpp:127) | Configure static IP or DHCP |
| `parseIp(s, out)` | `applyStaStaticIp`, `applyEthStaticIp` | Parse dotted-quad IP string |

## 8. Data Flow

### WiFi Station Bring-Up (setup, core 0)

1. Check BOOT button: `digitalRead(0)` held LOW for up to 3 s → force portal: `net_state.cpp:108-114`
2. If `cfg.staticIp` set without creds → clear static: `net_state.cpp:115-116`
3. `WiFi.mode(WIFI_STA)`, set hostname: `net_state.cpp:116-117`
4. If no SSID → migrate WiFi creds from NVS (legacy WiFiManager): `net_state.cpp:118`
5. If no creds or forced portal → `startSetupPortal()`: `net_state.cpp:119-122`
6. `applyStaStaticIp()` — DHCP or static: `net_state.cpp:127`
7. `WiFi.begin(ssid, psk)` — 30 s connection window with `bootConnectingLed()`: `net_state.cpp:128-134`
8. On failure with `autoIpFallback`: deterministic AutoIP `169.254.<third>.<fourth>` based on MAC: `net_state.cpp:136-163`
9. On failure without AutoIP: `startSetupPortal()`: `net_state.cpp:173-175`
10. On success: `connectStrongestAP()`, `WiFi.setSleep(WIFI_PS_NONE)`, log + syslog: `net_state.cpp:177-182`

### Setup Portal

1. `startSetupPortal()` starts WiFi AP (open, no password): `setup_portal.cpp:10-11`
2. Starts captive DNS redirecting all queries to the AP IP: `setup_portal.cpp:12`
3. `g_setupPortal = true` → `loop()` pumps DNS: `main.cpp:138`
4. `web_server.cpp:81-90` — `onNotFound` redirects to `/` when portal is active

## 9. Protocol Layout

N/A (no wire protocol — WiFi/Ethernet abstraction over standard protocols).

## 10. Configuration Integration

| Config Field | Source | Usage | Flags |
|---|---|---|---|
| `hostname` | `config_schema.cpp:47` | WiFi hostname, mDNS, AP SSID: `net_state.cpp:117,222` | `CFG_LIVE` |
| `wifiMode` | `config_schema.cpp:104` | STA (0) vs AP (1): `main.cpp:71` | `CFG_REBOOT` |
| `wifiSsid` | `config_schema.cpp:105` | WiFi credentials: `net_state.cpp:128-129` | `CFG_SECRET` |
| `wifiPsk` | `config_schema.cpp:106` | WiFi password | `CFG_SECRET` |
| `staticIp` | `config_schema.cpp:109` | Static vs DHCP: `net_state.cpp:81` | `CFG_REBOOT` |
| `ip`, `gateway`, `subnet`, `dns` | `config_schema.cpp:110-113` | Static IP config: `net_state.cpp:82-85` | `CFG_REBOOT` |
| `autoIpFallback` | `config_schema.cpp:115` | 169.254.x.x on DHCP fail: `net_state.cpp:136` | `CFG_LIVE` |
| `apPassword` | `config_schema.cpp:108` | AP WPA2 password: `net_state.cpp:215` | `CFG_SECRET` |
| `useEthernet` | `config_schema.cpp:103` | Wired path: `main.cpp:59,63` | `CFG_REBOOT` |
| `linkLossMode` | `config_schema.cpp:107` | Link-loss policy: `ethernet.cpp:121-143` | `CFG_REBOOT` |

## 11. Lifecycle

1. **Setup phase 4 (main.cpp:57-75):** WiFi AP, WiFi station, or wired Ethernet bring-up based on config
2. **mDNS (setup, main.cpp:79-103):** MDNS registered after network is up
3. **Loop (main.cpp:136-138):** Setup portal DNS pump: `dnsServer.processNextRequest()` when `g_setupPortal`
4. **netRxTask (tasks.cpp:146-155):** Network packet processing on core 0

## 12. Error Handling

| Condition | Handling | Source |
|---|---|---|
| BOOT button held (GPIO0 LOW) | Forces setup portal | `net_state.cpp:108-114` |
| No WiFi credentials | Starts setup portal | `net_state.cpp:119-122` |
| WiFi connect timeout (30 s) | AutoIP fallback if enabled, else setup portal | `net_state.cpp:135-176` |
| AutoIP DHCP retry fails (10 s) | Stays on link-local address | `net_state.cpp:157-166` |
| AP password < 8 chars | Refuses to start AP when `requirePw=true` | `net_state.cpp:214-218` |
| Non-local subnet ArtIpProg | Logs and ignores | `artnet_bridge.cpp:122-124` |

## 13. Memory Allocation

- All global flags are static `bool` (net_state.cpp:12-16) — 5 bytes of BSS
- `dnsServer` is a global `DNSServer` (setup_portal.cpp:6) — static allocation
- `WiFi.begin` connection window uses stack-allocated `IPAddress` objects in `connectStrongestAP` (`net_state.cpp:191-200`)
- No heap allocation in the normal path.

## 14. Timing

| Operation | Budget | Source |
|---|---|---|
| WiFi connect attempt | 30 000 ms | `net_state.cpp:131` |
| BOOT button hold | 3 000 ms | `net_state.cpp:111` |
| AutoIP DHCP retry | 10 000 ms | `net_state.cpp:155` |
| Strongest AP reconnect | 12 000 ms | `net_state.cpp:207` |

WiFi `setSleep(WIFI_PS_NONE)` is set after successful connection (`net_state.cpp:161,178`) to prevent WiFi power-save from introducing latency into the 2 ms `netRxTask` loop.

## 15. Traceability / Evidence

| Claim | Source |
|---|---|
| Global mode flags `g_apMode`, `g_useEth`, etc. | `net_state.cpp:12-16` |
| `g_setupPortal` gated DNS pump in `loop()` | `main.cpp:138` |
| Captive DNS redirect in `onNotFound` | `web_server.cpp:81-90` |
| Boot button (GPIO0) forces portal | `net_state.cpp:108-114` |
| AutoIP deterministic from MAC | `net_state.cpp:139-149` |
| `connectStrongestAP` scans and roams | `net_state.cpp:185-212` |
| WiFi `setSleep(WIFI_PS_NONE)` after connect | `net_state.cpp:161,178` |
| `netIsLocalSubnet` checks local subnet + 169.254.x.x | `net_state.cpp:71-78` |
| `migrateWifiCredsFromNvs` legacy WiFiManager migration | `net_state.cpp:89-104` |
| AP mode uses hostname as SSID | `net_state.cpp:222` |

## 16. Cross-References

- [Ethernet](./net-ethernet.md) — wired bring-up, called from `main.cpp:64`
- [Setup Portal](./net-setup-portal.md) — captive DNS portal
- [LED Status](./sys-led-status.md) — `bootConnectingLed()` during WiFi connect
- [Syslog](./sys-syslog.md) — `syslogPrintf(SYSLOG_NOTICE, ...)` on WiFi connect
- [Art-Net Protocol](./net-artnet-protocol.md) — uses `netLocalIP`/`netSSID` for PollReply
- [ArtNet Bridge](./net-artnet-bridge.md) — `netIsLocalSubnet` for ArtIpProg validation
- [Task Scheduling](./sys-tasks.md) — `netRxTask` on core 0

## 17. Limitations

- No WiFi disconnection/reconnection handler visible in the inspected source — only the initial connect path in `startWiFiStation` is implemented. Not determinable from the inspected source code — whether WiFi drop+reconnect logic exists outside `startWiFiStation`.
- The 30-second connect timeout (`net_state.cpp:131`) blocks `setup()`; no async reconnect path is visible.

## 18. Open Questions

- Not determinable from the inspected source code — whether WiFi disconnection or roaming is handled after initial connect (no `WiFiEvent` handler found in the inspected source).
- Not determinable from the inspected source code — the exact DHCP failure detection (WiFi.begin returns before DHCP completes; the `WL_CONNECTED` check may only reflect association, not address assignment).

## 19. Testing

No dedicated unit test for `net_state.cpp`. WiFi/Ethernet bring-up is validated through hardware integration testing. No native host test covers the network state abstractions.

## 20. History

No recorded changes.
