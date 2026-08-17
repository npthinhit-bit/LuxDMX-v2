Setup Portal — Technical Reference

Domain: `net.setup-portal`

## 1. Domain Scope

Captive-portal setup flow for first-run configuration or BOOT-button-triggered recovery. When the device has no WiFi credentials or the user holds the BOOT button during startup, the firmware starts a standalone WiFi AP with a wildcard DNS redirect, routing all HTTP requests to `/` where the setup page serves a WiFi credential form.

**Owns:** `startSetupPortal()`, `dnsServer` instance, `g_setupPortal` flag interaction.
**Delegates to:** `net_state.cpp::startWiFiAP` (AP bring-up), `web_server.cpp::onNotFound` (captive redirect), `cfgserial` / `cfgcore` (config persistence after setup).
**Consumed by:** `main.cpp` (setup), `loop()` (DNS pump), `web_server.cpp` (404 redirect).

## 2. Layer Mapping

| Layer | Directory | Role |
|---|---|---|
| net | `src/net/` | First-run setup portal (captive DNS + AP) |

## 3. Source Files

| File | Role |
|---|---|
| `src/net/setup_portal.cpp` | `startSetupPortal` implementation, global `dnsServer` |
| `src/net/net_state.h` | `g_setupPortal` flag declaration, `startSetupPortal` declaration |
| `src/net/net_state.cpp` | Caller: `startSetupPortal()` called from `startWiFiStation` at lines 121, 174 |
| `src/main.cpp` | DNS pump in `loop()`: `dnsServer.processNextRequest()` at line 138 |
| `src/net/web_server.cpp` | Captive redirect in `onNotFound` at lines 81-90 |
| `src/frontend/web_frontend.h` | `web_frontend.h` serves the setup HTML page |

## 4. Data Structures

### Global State (net_state.h:29-30, setup_portal.cpp:6)

| Variable | Type | File | Description |
|---|---|---|---|
| `g_setupPortal` | `bool` | net_state.cpp:16 | True when the captive portal is active |
| `dnsServer` | `DNSServer` | setup_portal.cpp:6 | Wildcard DNS on port 53 |

### Setup Portal State (implicit)

No explicit state struct. The portal is active for the entire boot session until `loop()` DNS pump stops (when `g_setupPortal` is set to `false` — transition not visible in inspected source).

## 5. Concurrency

Single-threaded on core 0. `startSetupPortal()` is called from `startWiFiStation` during `setup()` (before tasks spawn). The DNS pump runs in `loop()` on core 0 (`main.cpp:138`). The `onNotFound` handler runs on the AsyncWebServer task (core 0, AsyncTCP pinned to core 0 via `CONFIG_ASYNC_TCP_RUNNING_CORE=0`).

## 6. State Machine

```
[Normal boot]
  ↓
SSID empty OR BOOT button held?
  ↓ yes
[startSetupPortal()]
  → g_setupPortal = true
  → startWiFiAP(false)  // open AP, no password
  → dnsServer.start(53, "*", softAPIP)  // wildcard DNS
  ↓
[Setup Portal Active]
  → loop() pumps DNS: dnsServer.processNextRequest()
  → onNotFound redirects all unknown paths to /  (captive)
  → / serves setup HTML page (WiFi credential form)
  ↓
User submits credentials → /setup POST
  → cfgcore::setValue("wifiSsid/Psk")
  → saveConfig() to NVS
  → g_setupPortal = false  (transition: reboot to apply)
  ↓
[Reboot into normal WiFi station mode]
```

## 7. Entry Points

| Function | Called from | Purpose |
|---|---|---|
| `startSetupPortal()` | `net_state.cpp:121,174` (`startWiFiStation`) | Start AP + wildcard DNS |
| `dnsServer.processNextRequest()` | `main.cpp:138` (`loop`) | DNS pump (captive redirect) |
| `onNotFound` handler | `web_server.cpp:81-90` (`webRegisterRoutes`) | HTTP 302 redirect to `/` |

## 8. Data Flow

1. **Trigger:** `startWiFiStation` finds no SSID or BOOT held → calls `startSetupPortal()`: `net_state.cpp:119-122`
2. **AP start:** `startSetupPortal` calls `startWiFiAP(false)` (open, no password required): `setup_portal.cpp:10`
3. **DNS start:** `dnsServer.start(53, "*", WiFi.softAPIP())` — wildcard DNS redirects all queries to the AP IP: `setup_portal.cpp:12`
4. **DNS pump:** `loop()` calls `dnsServer.processNextRequest()` while `g_setupPortal` is true: `main.cpp:138`
5. **Captive redirect:** `web_server.cpp:81-90` — `onNotFound` sends HTTP 302 to `/` when `g_setupPortal`
6. **Setup page:** HTTP GET `/` serves the setup HTML page (served by `web_frontend.cpp`)
7. **Configuration:** User submits WiFi credentials via POST `/setup` → `web_routes.cpp` handler persists to NVS and reboots

The WiFi credentials are persisted via `cfgcore::save()` / `saveConfig()` to the `"dmxgw"` NVS namespace. On reboot, `startWiFiStation` finds the saved SSID and connects normally.

## 9. Protocol Layout

N/A (no wire protocol — the portal uses standard HTTP/HTTPS redirected via DNS wildcard).

## 10. Configuration Integration

| Config Field | Source | Usage | Flags |
|---|---|---|---|
| `wifiSsid` | `config_schema.cpp:105` | Empty → triggers portal | `CFG_NONE` |
| `wifiPsk` | `config_schema.cpp:106` | Persisted after setup | `CFG_SECRET` |
| `hostname` | `config_schema.cpp:47` | Used as AP SSID | `CFG_LIVE` |
| `apPassword` | `config_schema.cpp:108` | Not enforced (`requirePw=false`) in portal mode | `CFG_SECRET` |
| `staticIp` | `config_schema.cpp:109` | Cleared if BOOT+portal (`net_state.cpp:115`) | `CFG_REBOOT` |

## 11. Lifecycle

1. **Init (setup):** `startSetupPortal()` called from `startWiFiStation` when no creds or BOOT held: `setup_portal.cpp:8-16`
2. **Runtime (loop):** DNS pump every loop iteration while `g_setupPortal`: `main.cpp:138`
3. **Web redirect:** `onNotFound` handler active while `g_setupPortal`: `web_server.cpp:81-90`
4. **Exit:** User configures WiFi via the setup page → config saved → reboot → normal WiFi station mode

## 12. Error Handling

| Condition | Handling | Source |
|---|---|---|
| AP start fails | Serial logged, DNS not started | `setup_portal.cpp:11` (checks `ok`) |
| DNS start fails | Not explicitly checked — `dnsServer.start` return ignored | `setup_portal.cpp:12` |

## 13. Memory Allocation

- `dnsServer` — global `DNSServer` object, statically allocated (setup_portal.cpp:6)
- `g_setupPortal` — global `bool`, 1 byte of BSS (net_state.cpp:16)
- DNS query buffer — internal to `DNSServer` (Arduino-ESP32)

## 14. Timing

**BOOT button hold:** 3-second window (`net_state.cpp:111`) — if GPIO0 is held LOW for 3 s, the setup portal is forced even if WiFi creds exist.

**DNS propagation:** `dnsServer.processNextRequest()` is called every `loop()` iteration (non-blocking) while `g_setupPortal` is true (`main.cpp:138`). There is no dedicated task for the DNS pump.

## 15. Traceability / Evidence

| Claim | Source |
|---|---|
| `startSetupPortal` starts AP + wildcard DNS | `setup_portal.cpp:8-16` |
| Starts AP with `requirePw=false` (open) | `setup_portal.cpp:10` |
| Wildcard DNS on port 53 | `setup_portal.cpp:12` |
| DNS pump in `loop()` while `g_setupPortal` | `main.cpp:138` |
| `g_setupPortal` flag declared in net_state.h | `net_state.h:29` |
| Captive redirect in `onNotFound` | `web_server.cpp:81-90` |
| Called from `startWiFiStation` when no creds | `net_state.cpp:121` |
| Called from `startWiFiStation` on connect failure | `net_state.cpp:174` |
| BOOT button (GPIO0) holds for 3 s | `net_state.cpp:108-114` |
| Static IP cleared on forced portal | `net_state.cpp:115` |

## 16. Cross-References

- [Net State](./net-net-state.md) — `g_setupPortal` flag, `startWiFiAP` caller
- [Ethernet](./net-ethernet.md) — `applyWiredLinkLoss` also calls `startWiFiAP`
- [Web Server](./net-web-server.md) — `onNotFound` captive redirect at `web_server.cpp:81-90`
- [Web Routes](./net-web-routes.md) — `/setup` POST handler processes credentials
- [Web Frontend](./net-web-frontend.md) — serves the setup HTML page
- [Config Engine](./config-engine.md) — `saveConfig()` persists WiFi credentials to NVS

## 17. Limitations

- The captive DNS uses a single `DNSServer` instance with a fixed max of 5 concurrent queries (set at `dnsServer.start(53, "*", ...)` — the `53` is the port, the query limit is internal to `DNSServer`).
- The setup portal serves the setup HTML page but the credential POST handler is not in `setup_portal.cpp` — it is in `web_routes.cpp` (not determinable which exact handler processes the setup form submission from the inspected `setup_portal.cpp` alone).

## 18. Open Questions

- Not determinable from the inspected source code — which `web_routes.cpp` handler processes the setup form POST and calls `saveConfig()`.
- Not determinable from the inspected source code — the exact HTML form field names expected by the setup page (defined in frontend HTML, not in the C++ source).
- Not determinable from the inspected source code — whether the setup portal supports WiFi scanning (the `/setup/scan` route exists at `web_server.cpp:50` but the implementation is in `web_routes.cpp`).

## 19. Testing

No dedicated unit test for `setup_portal.cpp`. The captive portal flow is validated through manual testing on a live device.

## 20. History

No recorded changes.
