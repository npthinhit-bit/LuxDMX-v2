# OTA Sign — System Specification

Domain: net.ota-sign

## 1. Module Overview

The OTA Sign subsystem owns the on-device cryptographic verification of firmware
images before they are committed to flash and booted. It verifies an Ed25519
signature appended to every release firmware image, ensuring that only
manufacturer-signed firmware can run on production devices.

The module is invoked by the OTA subsystem after a firmware image has been fully
streamed into the ESP-IDF OTA update partition and before the update session is
finalized. The verification process:

1. Locates the next OTA update partition.
2. Extracts the 64-byte Ed25519 signature from the tail of the image.
3. Computes a SHA-256 hash over the firmware bytes (excluding the signature).
4. Imports the embedded raw 32-byte Ed25519 public key into the ESP-IDF PSA
   crypto service.
5. Verifies the signature against the SHA-256 digest using PSA PureEdDSA.
6. On failure, rolls the boot partition back to the currently running application
   so a tampered, corrupt, or bad-signed image can never boot.

The module is gated by a compile-time flag (`OTA_SIGN_ENABLED`): when disabled,
verification is skipped and all images are accepted unconditionally. This allows
development builds to bypass the signature check.

**Owns:** Ed25519 signature verification, SHA-256 image hashing, and explicit
running-partition restoration on verification failure.
**Delegates to:** ESP-IDF partition APIs and PSA crypto primitives.
**Consumed by:** OTA subsystem, which calls `otaVerifyPartition()` after
`esp_ota_end()` and before selecting the new boot target.

## 2. External Interfaces

### 2.1 Entry Points

| Entry point | Caller | Purpose |
|---|---|---|
| `otaVerifyPartition()` | OTA subsystem after `esp_ota_end()` | Reads the staged partition, hashes the image excluding its trailing signature, and verifies the signature. On failure, restores the running boot partition and returns `false`; it never selects a new boot target. |
| `otaVerifySignature(hash, sig, public_key)` | `otaVerifyPartition()` | Imports the raw public key with PSA and verifies the 64-byte Ed25519 signature against the 32-byte SHA-256 digest. |

### 2.2 Firmware Image Layout

| Offset | Size | Field |
|---|---|---|
| 0 | variable | Firmware image bytes (DFU/OTA) |
| `imageSize - 64` | 64 bytes | Ed25519 signature (R ? S) |

The signature is the final 64 bytes of the image. The hash is computed over bytes
`[0, imageSize - 64)` — i.e., the entire firmware image excluding the trailing
signature.

### 2.3 Cryptographic Constants

| Constant | Value | Description |
|---|---|---|
| Public key size | 32 bytes | Raw Ed25519 public key embedded at build time |
| Signature size | 64 bytes | Ed25519 signature (R ? S), appended to the firmware image |
| Hash size | 32 bytes | SHA-256 digest of the firmware image |
| Minimum image size for verification | 64 bytes | Images smaller than the signature size are rejected |
| Hash chunk size | 1,024 bytes | Flash read granularity during SHA-256 computation |

### 2.4 Build-Time Configuration

| Build Profile | `OTA_SIGN_ENABLED` | Behavior |
|---|---|---|
| Dev targets (`esp32dev`, `wt32eth01`, `esp32s3_psram`) | 0 (disabled) | Verification is explicitly bypassed for development only |
| Release targets (`*_release`) | 1 (enabled) | Full Ed25519 verification is enforced |
| `CONFIG_LUXDMX_OTA_SIGN_ENABLED` | default `n` | Kconfig fallback remains development-safe; release profiles define `OTA_SIGN_ENABLED=1` |

The source default is fail-closed (`1`) only when no Kconfig symbol is supplied. The
checked-in development profiles provide the Kconfig symbol with its default `n`,
while release profiles explicitly define `OTA_SIGN_ENABLED=1`.

## 3. State Machine

No state machine — the module is synchronous: verify-or-reject, called-and-returned.

`otaVerifyPartition()` produces one of two outcomes:

| Return | Meaning |
|---|---|
| `true` | Signature valid; the staged partition is still not selected. The caller then calls `esp_ota_set_boot_partition()` and restarts. |
| `false` | Signature invalid or any I/O/crypto error; the running partition is restored as boot target. The caller sets OTA phase to Error. |

## 4. Data Flow

1. **Locate update partition:** `esp_ota_get_next_update_partition(NULL)` retrieves
   the next OTA update partition. If NULL (no partition available), verification
   fails and returns `false`.
2. **Determine image size:** The total firmware image size — set by the OTA
   subsystem's `Update.begin()` call — is obtained. If the size is unknown or
   less than the 64-byte signature, verification fails and returns `false`.
3. **Extract signature:** The signature offset is computed as `imageSize - 64`.
   The 64-byte signature is read from the tail of the update partition.
4. **Hash firmware (SHA-256):** A SHA-256 context is initialized. The firmware
   image bytes (from offset 0 up to the signature offset) are read from flash in
   1,024-byte chunks and fed into the SHA-256 update function. The digest is
   finalized, producing a 32-byte hash.
5. **Import public key:** PSA imports the raw 32-byte key as an
   `ECC_FAMILY_TWISTED_EDWARDS` public key with `PSA_ALG_PURE_EDDSA` usage.
6. **Verify signature:** `psa_verify_message` verifies the 64-byte signature over
   the 32-byte SHA-256 digest. The temporary PSA key is destroyed after use.
7. **Rollback on failure:** If verification returns non-zero, the module retrieves
   the currently running partition and sets it as the boot partition via the ESP-IDF
   OTA operations API, reversing the pending update. It then returns `false`.
9. **Success:** The update partition remains staged but is not selected by the
   verifier. The OTA subsystem calls `esp_ota_set_boot_partition()` only after
   `true`, then sends the response and reboots.

The host-side signing tool computes the same SHA-256 digest over the firmware bytes,
signs the 32-byte digest with Ed25519, and appends the 64-byte signature to the
image — producing the layout this module verifies.

## 5. Configuration Integration

The module reads no configuration-engine fields. The build-time controls are
`OTA_SIGN_ENABLED` (release override) and `CONFIG_LUXDMX_OTA_SIGN_ENABLED`:


- **Dev targets** use the Kconfig default `n` and intentionally bypass verification.
- **Release targets** set `OTA_SIGN_ENABLED=1`, enforcing verification on every
  update. Release CI requires the `LUXDMX_OTA_PRIVATE_KEY` secret and checks that
  it matches the embedded public key before publishing artifacts.

The 32-byte public key is a static constant compiled into the firmware. It is
generated by the host-side key-generation tooling and must be kept in sync with the
corresponding private key used by the host-side signing tool.

## 6. Lifecycle

- **Init:** No persistent initialization. The public key is a static constant array;
  PSA crypto is initialized and the temporary key is destroyed per verification.
- **Verify:** Called after `esp_ota_end()` and after all bytes are present in the
  staged partition.
- **Commit/rollback:** Verification does not select a boot target. The OTA caller
  selects the staged partition only after `true`; failures explicitly restore the
  running partition.
- **Shutdown:** No shutdown hook — the function is called-and-returned.

## 7. Error Handling

All failures return `false`. The OTA subsystem caller sets the update phase to
Error (4) on a `false` return.

| Failure | Behavior |
|---|---|
| Update partition is NULL | Return `false` immediately |
| Image size unknown or < 64 bytes | Return `false` immediately |
| Signature read from flash fails | Return `false` |
| SHA-256 initialization fails | Free context; return `false` |
| Flash read during hashing fails | Free context; return `false` |
| SHA-256 update fails | Free context; return `false` |
| SHA-256 finalization fails | Free context; return `false` |
| PSA public-key import fails | Destroy temporary key/attributes; return `false` |
| Signature verification fails (`psa_verify_message` not successful) | Destroy temporary key; restore boot partition to running app; return `false` |
| `OTA_SIGN_ENABLED` is 0 | Both functions return `true` unconditionally — verification skipped |

## 8. Timing Constraints

| Item | Value |
|---|---|
| SHA-256 chunk size (flash reads) | 1,024 bytes |
| Signature size | 64 bytes |
| Hash size (SHA-256) | 32 bytes |
| Public key size | 32 bytes |
| Minimum image size for verification | 64 bytes |

The hashing loop reads 1 KB at a time from flash. Duration scales linearly with
firmware image size — for a ~500 KB image, approximately 500 read+hash iterations
execute. There is no hard deadline; verification runs to completion on the core-0
OTA worker task. No progress callback is available to the web UI during
verification, so the browser cannot distinguish verification time from download
time.

## 9. Memory and Allocation Model

All buffers are stack-allocated; no heap usage:

| Buffer | Size | Lifetime |
|---|---|---|
| `sig` | 64 bytes | Per verification call (stack) |
| `hash` | 32 bytes | Per verification call (stack) |
| `buf` | 1,024 bytes | Per verification call (stack) |
| PSA hash operation | internal | Per verification call |
| PSA key attributes/key id | internal | Per verification call |
| `OTA_PUBLIC_KEY` | 32 bytes | Static const (ROM/flash) |

No PSRAM usage. No heap allocation.

## 10. Safety Considerations

- **Signature-first commitment:** The update partition is never set as the boot
  target until after signature verification passes. A bad-signed or tampered image
  can never boot.
- **Rollback on failure:** When verification fails, the boot partition is explicitly
  reset to the currently running application. The device will reboot into the last
  known-good firmware, not the failed image.
- **Crash guard integration:** The boot-retry counter (owned by the OTA subsystem)
  provides a second layer of defense — if a good-signed but buggy image boots and
  then crashes, the counter detects repeated failures and resets, allowing fallback
  to the last-good partition.
- **Build-time disable for dev:** The `OTA_SIGN_ENABLED` flag allows development
  builds to skip verification, but release builds define it as 1 and must use a
  matching protected signing key.
- **No partition erase on rollback:** The rollback path sets the boot partition to
  the running app but does not erase the failed update partition. Stale data may
  persist, but the ESP-IDF Update framework re-initializes the partition on the next
  `Update.begin` call.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| net.ota | upstream consumer | Calls `otaVerifyPartition()` after streaming and `esp_ota_end()`, before boot-target selection |
| net.rate-limiter | upstream guard | The OTA Rate Limiter gates the upload/streaming triggers before verification is ever reached |
| sys.firmware-version | upstream peer | The version-check task sets `updateAvailable`, which may trigger an auto-fetch (currently not wired) |
| platformio.ini | build-time | Defines explicit dev and `*_release` profiles; release profiles set `OTA_SIGN_ENABLED=1` |
| `tools/gen_ota_keys.py`, `tools/sign_ota_image.py` | host tooling | Generates public header, signs `SHA256(firmware.bin)`, and checks key/header match |

## 12. Testing Verification

Host coverage is provided by `tools/test_ota_sign.py`, which checks the
SHA-256-plus-Ed25519 append contract, tamper rejection, and empty-image rejection.
The native C suite separately covers the pure boot-retry policy. ESP-IDF PSA and
partition I/O remain device-gated.

Verification is validated end-to-end (not unit-level) through:

- The host-side signing tool signs a firmware image; the device accepts (boot) or
  rejects (rollback) the image, validating the round-trip SHA-256 + Ed25519 path.
- The 5-minute firmware evaluation workflow: flash a signed build, monitor serial
  logs for successful boot, and confirm the web interface is reachable.
- Deliberately bad-signed images are used to validate the rollback path.

**Remaining device-gated paths:**
- `otaVerifyPartition()` against an ESP-IDF flash partition with a production key.
- PSA/flash-read error handling and actual boot-partition restoration.
- Bootloader pending-verify rollback after a deliberately crashing signed image.
- Hardware acceptance of a signed artifact on each supported board.

## 13. Open Questions

1. The checked-in public key is currently a non-production placeholder. Before
   publishing a real release, generate a protected key with `tools/gen_ota_keys.py`,
   replace `ota_key.h`, and store only the matching private PEM as the
   `LUXDMX_OTA_PRIVATE_KEY` GitHub secret. Key rotation requires rebuilding
   firmware with the new public key.
2. Whether a future refactor plans to make `OTA_SIGN_ENABLED` a runtime-configurable
   flag rather than a compile-time constant, allowing operators to enable/disable
   verification without a rebuild.
3. Whether PSA crypto support remains enabled in every future ESP-IDF target
   configuration; the current three release builds compile successfully with
   ESP-IDF 6.0.1, but hardware verification is still required.
4. Whether the 1,024-byte hashing chunk size is optimal for flash read performance,
   or whether a larger buffer would reduce iteration count for large images.
5. Whether the rollback path should also erase the failed update partition to
   prevent stale data from confusing a subsequent OTA.

## 14. History

- Signature verification extracted into a dedicated module. Functions
  `otaVerifySignature` and `otaVerifyPartition` form the public API.
- The implementation uses ESP-IDF PSA PureEdDSA over a SHA-256 digest and hashes
  staged partitions in 1 KiB reads without retaining the image in RAM.
- Host key generation/signing tooling and tag-only signed release jobs were added;
  the release job fails closed when the protected private-key secret is absent or
  mismatched.
- `OTA_SIGN_ENABLED` compile-time gate added so development environments can disable
  verification; explicit `*_release` profiles enable it and CI signs artifacts only
  when the protected key secret is present.
- Rollback path added: `esp_ota_set_boot_partition` to the running partition on
  verification failure, preventing a bad signature from bricking the device.
- SHA-256 chunked hashing loop (1 KB buffer) added to avoid loading the entire
  firmware image into DRAM during verification.
