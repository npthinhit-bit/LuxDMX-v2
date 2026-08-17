# Setup Portal â€” System Specification

Domain: net.setup-portal

## 1. Module Overview

The Setup Portal is a first-run configuration and recovery mechanism that provides a captive DNS portal when the device has no WiFi credentials or when the user holds the BOOT button during startup. It activates a standalone WiFi access point (open, SSID set to the device hostname) with a wildcard DNS server that redirects all DNS queries to the access-point IP, causing any HTTP request to be routed to the setup page served at `/` where the user can submit WiFi credentials.

**Owns:** the captive DNS server instance, the setup-portal activation flag, the AP bring-up call when portal mode is engaged.
**Delegates to:** WiFi AP bring-up, web server not-found handler (captive redirect), config persistence after setup submission.
**Consumed by:** system bring-up (`setup`), main loop (DNS pump), web server (404 redirect).

## 2. External Interfaces

### Captured DNS Server

| Component | Type | Description |
|---|---|---|
| DNS server instance | DNSServer | Wildcard DNS on port 53, redirecting all queries to the AP IP |

### Setup Portal Flag

| Flag | Type | Description |
|---|---|---|
| Setup portal active | bool | True when the captive portal is active; drives the main-loop DNS pump and the web server's not-found redirect |

### Activation Conditions

The portal starts under any of these conditions:
- No WiFi SSID is configured (empty credentials).
- The BOOT button (GPIO0) is held LOW for 3 seconds during startup.
- A prior WiFi connection attempt failed and the AutoIP fallback is disabled.

## 3. State Machine

```
[Normal boot]
  |
  +-- SSID empty OR BOOT held OR connect failed (no AutoIP) -->
        |
        v
  [startSetupPortal()]
    -> Setup portal active = true
    -> WiFi AP started (open, SSID = hostname)
    -> Wildcard DNS: all queries -> AP IP
    |
    v
  [Portal Active]
    -> Main loop pumps DNS (processNextRequest per iteration)
    -> Web not-found handler redirects all paths to /
    -> GET / serves the setup HTML page (WiFi credential form)
    |
    v
  User submits credentials -> POST /setup
    -> Credentials persisted to NVS
    -> Portal flag cleared
    -> Reboot into normal WiFi station mode
```

## 4. Data Flow

1. **Trigger:** During WiFi station bring-up, if no SSID is found or the BOOT button (GPIO0) is held for 3 seconds, the setup portal is started.
2. **AP start:** An open WiFi access point is started using the configured hostname as the SSID (no password required).
3. **DNS start:** A wildcard DNS server is started on port 53, redirecting all queries to the access-point IP address.
4. **DNS pump:** The main loop calls the DNS server's next-request processor every iteration while the portal is active. This is non-blocking.
5. **Captive redirect:** The web server's not-found handler sends an HTTP 302 redirect to `/` when the portal is active, ensuring all HTTP traffic lands on the setup page.
6. **Setup page:** An HTTP GET request to `/` serves the setup HTML page containing the WiFi credential form.
7. **Configuration:** The user submits WiFi credentials via a POST to `/setup`; the credentials are persisted to NVS, the portal flag is cleared, and the device reboots into normal WiFi station mode.

## 5. Configuration Integration

| Config Field | Apply Semantics | Usage |
|---|---|---|
| wifiSsid | secret | Empty value triggers portal activation |
| wifiPsk | secret | Persisted to NVS after setup |
| hostname | live | Used as the AP SSID and mDNS hostname |
| apPassword | secret | Not enforced in portal mode (AP is open) |
| staticIp | reboot | Cleared when the portal is force-started via BOOT hold |

## 6. Lifecycle

1. **Init (setup):** The setup portal is started from the WiFi station bring-up routine when no credentials exist, the BOOT button is held, or the connection attempt fails without AutoIP fallback. The AP is opened, and the wildcard DNS server is started.
2. **Runtime (loop):** The DNS server is pumped every main-loop iteration while the portal flag is active.
3. **Web redirect:** The web server's not-found handler is active while the portal flag is set, redirecting unmatched requests to `/`.
4. **Exit:** The user configures WiFi credentials via the setup page; credentials are saved to NVS and the device reboots into normal WiFi station mode.

## 7. Error Handling

| Condition | Handling |
|---|---|
| AP start fails | Logged to serial; DNS server is not started |
| DNS start fails | DNS start return value is not checked; portal remains inactive for DNS (captive redirect via web handler still functions) |

## 8. Timing Constraints

| Operation | Value |
|---|---|
| BOOT button hold detection | 3-second window; if GPIO0 is held LOW for 3 seconds, the setup portal is forced even if WiFi credentials exist |
| DNS processing | `processNextRequest()` is called every main-loop iteration (non-blocking) while the portal is active; no dedicated task is needed |

## 9. Memory and Allocation Model

- **DNS server:** Statically allocated global `DNSServer` instance (no heap).
- **Portal flag:** Single boolean global, 1 byte of BSS.
- **DNS query buffer:** Managed internally by the `DNSServer` implementation (fixed-size buffer, static allocation).

No heap allocation occurs during portal operation.

## 10. Safety Considerations

- The access point is opened with no password, which is acceptable only during first-run setup or recovery; a user must provision real credentials before leaving the device unattended on a network.
- The wildcard DNS redirects all queries to the AP IP, ensuring captive-portal detection works on mobile devices and laptops; this DNS scope is bounded to the AP subnet only.
- The 3-second BOOT-hold requirement prevents accidental portal activation during normal operation.
- If AP or DNS startup fails, the device falls back to the normal boot path (no portal) rather than hanging, preserving the ability to connect via serial console.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| WiFi AP bring-up | downstream | Starts the open access point with hostname as SSID |
| Web Server | downstream (consumed) | Not-found handler performs captive redirect to `/` |
| Web Frontend | downstream | Serves the setup HTML page with WiFi credential form |
| Web Routes | downstream | Processes the `/setup` POST and persists credentials to NVS |
| Config Engine | upstream (consumed) | Saves WiFi credentials to the `"dmxgw"` NVS namespace |
| Network State | downstream | AP bring-up entry point called when portal is activated |
| Sys Tasks | upstream (indirect) | AsyncTCP task on core 0 provides the web server execution context |

## 12. Testing Verification

No host-native unit test covers the setup portal module. The captive DNS redirect, AP bring-up, and credential persistence flow are validated through manual testing on a live device â€” provisioning WiFi credentials via the portal and confirming a reboot into normal station mode.

## 13. Open Questions

1. Which web route handler processes the setup form POST and invokes config persistence.
2. The exact HTML form field names expected by the setup page.
3. Whether the setup portal supports WiFi scanning (a `/setup/scan` route exists but the implementation location is not confirmed).

## 14. History

No recorded changes.
