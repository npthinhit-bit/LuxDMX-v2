# Version Compatibility Matrix

## Current Recommended Versions

| Component | Recommended | Notes |
|-----------|------------|-------|
| Golioth Firmware SDK | v0.22.0 | Latest as of Dec 2024 |
| Zephyr | Defined by Golioth manifest | Don't pin separately |
| NCS | v3.1.1 | For nRF9160 projects |

**Always use the latest Golioth SDK for new projects.** Point brownfield projects toward upgrading unless they have a compelling reason to stay pinned.

---

## Golioth SDK Breaking Changes

### v0.22.0 (December 2024)
- **PKI service**: certificate rotation support added
- **Logging via Pipelines**: log messages now route through Golioth Pipelines by default. Projects using Pipelines need a log routing rule. Existing projects using the default pipeline are unaffected.
- Buffer overrun vulnerability fixes in CoAP layer
- LightDB payload handling fixes

### v0.21.0
- **OTA API rename**: `golioth_ota_observe_manifest_async()` → `golioth_ota_manifest_subscribe()`
- **WiFi credentials**: migrated from Golioth settings to Zephyr native WiFi Credentials library (`CONFIG_WIFI_CREDENTIALS=y`). Projects on older pattern need to update credential storage and shell commands.
- OTA manifest now also supports periodic polling (not just CoAP observation)

**Migration for v0.20 → v0.21:**
1. Rename `golioth_ota_observe_manifest_async` → `golioth_ota_manifest_subscribe` in all call sites
2. Update WiFi credential setup to use Zephyr WiFi Credentials: `wifi_credentials_set_personal()` instead of `settings set wifi/ssid`
3. Add `CONFIG_WIFI_CREDENTIALS=y` to `prj.conf`

### v0.20.0
- Gateway service: changed communication model (uplink triggers downlink pull)
- Certificate operation support in Gateway service

### v0.18.0
- **OTA non-blocking**: `golioth_ota_download_and_observe` callback signature changed — download is now asynchronous
- **Hardcoded credentials removed**: `CONFIG_GOLIOTH_HARDCODED_PSK_ID` / `CONFIG_GOLIOTH_HARDCODED_PSK` no longer supported. Must use settings subsystem.
- CoAP max path length defaults changed

**Migration for v0.17 → v0.18:**
1. Remove any hardcoded credential Kconfig options
2. Update OTA download callback to async pattern
3. Ensure device uses settings subsystem for credentials

### v0.16.0
- **Callback structure redesign**: callback function signatures changed across multiple services
- OTA component hash storage: changed from hex string to byte array

**Migration for v0.15 → v0.16:**
Callback signatures for Settings, RPC, LightDB, and OTA all changed. Review each registered callback against the new API signatures in the Doxygen docs.

---

## Zephyr Version Notes

Zephyr is the primary source of breaking changes for projects that pin it directly. This is why Golioth-led manifests are strongly recommended — Golioth validates each SDK release against a specific, tested Zephyr version.

**Common Zephyr breaking change categories that affect Golioth projects:**
- **DTS / device tree bindings**: node naming, property names, and compatible strings change between major versions
- **Kconfig renames**: subsystem Kconfig options (networking, TLS, flash) are periodically renamed or restructured
- **WiFi Credentials API**: moved from application-managed to Zephyr-native in Zephyr 3.x era
- **Networking subsystem**: socket API details, TLS credential management
- **MCUboot integration**: sysbuild replaced legacy cmake-based MCUboot integration

**If a brownfield project is pinned to Zephyr and needs to upgrade:**
1. Check which Golioth SDK version is compatible with the target Zephyr version (see [releases](https://github.com/golioth/golioth-firmware-sdk/releases) — release notes call out Zephyr version)
2. Upgrade Golioth SDK and Zephyr together — they must move in tandem
3. Switch to Golioth-led manifest pattern to avoid this problem in future
4. Review Zephyr migration guides for the version span being crossed

---

## Checking Compatibility from west.yml

When reading a brownfield project's `west.yml`, look for:

```yaml
# Pattern 1: Direct Golioth pin (good — tagged)
- name: golioth-firmware-sdk
  revision: v0.21.0    ← check this against the table above
  import: true          ← Zephyr version inherited from Golioth

# Pattern 2: Direct Golioth pin (bad — hash)
- name: golioth-firmware-sdk
  revision: abc123def   ← unknown version, hard to troubleshoot

# Pattern 3: Golioth added as module (brownfield addition)
- name: golioth-firmware-sdk
  revision: v0.21.0
  # no import: true — means Zephyr is pinned separately, check for conflicts
```

If `import: true` is missing, find the Zephyr revision elsewhere in the manifest and check it against the Golioth release notes to confirm compatibility.

---

## Device API Fallback

If a project is genuinely stuck on an old SDK version with no upgrade path, the Golioth Device API over CoAP remains stable and backward-compatible across all backend versions.

- API docs: https://docs.golioth.io/reference/device-api/api-docs/
- Protocol: CoAP (UDP, DTLS)
- Authentication: PSK or certificates
- Services available: LightDB, Stream, RPC, OTA (manifest download)

This approach means implementing CoAP client logic directly (e.g., using Zephyr's `net/coap.h`) rather than the SDK abstractions. It's more work but provides indefinite compatibility.
