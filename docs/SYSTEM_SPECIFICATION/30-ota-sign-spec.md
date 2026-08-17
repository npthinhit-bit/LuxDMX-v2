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
4. Builds an ASN.1 SubjectPublicKeyInfo (SPKI) blob from the embedded 32-byte
   Ed25519 public key.
5. Verifies the signature against the hash using mbedTLS.
6. On failure, rolls the boot partition back to the currently running application
   so a tampered, corrupt, or bad-signed image can never boot.

The module is gated by a compile-time flag (`OTA_SIGN_ENABLED`): when disabled,
verification is skipped and all images are accepted unconditionally. This allows
development builds to bypass the signature check.

**Owns:** Ed25519 signature verification, SPKI key construction, SHA-256 image
hashing, partition rollback on verification failure.
**Delegates to:** none — uses ESP-IDF partition APIs and mbedTLS for cryptographic
primitives.
**Consumed by:** OTA subsystem (calls `otaVerifyAndCommit` after streaming
completes, before partition commit).

## 2. External Interfaces

### 2.1 Entry Points

| Entry point | Caller | Purpose |
|---|---|---|
| `otaVerifyAndCommit()` | OTA subsystem (upload final chunk, or after streaming completes) | Locates the update partition, extracts the signature, hashes the image, and verifies the signature. On failure, rolls back the boot partition and returns `false`. On success, returns `true`. |
| `otaVerifySignature(hash, sig)` | `otaVerifyAndCommit` (internal) | Parses the SPKI public key via mbedTLS, then verifies the 64-byte Ed25519 signature against the 32-byte SHA-256 hash. Returns `true` on valid signature, `false` otherwise. |

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
| SPKI total size | 44 bytes | 12-byte ASN.1 prefix + 32-byte public key |
| SPKI prefix | 12 bytes | ASN.1 SubjectPublicKeyInfo DER header wrapping the raw Ed25519 key |
| Minimum image size for verification | 64 bytes | Images smaller than the signature size are rejected |
| Hash chunk size | 1,024 bytes | Flash read granularity during SHA-256 computation |

### 2.4 Build-Time Configuration

| Build Profile | `OTA_SIGN_ENABLED` | Behavior |
|---|---|---|
| Dev / single-board targets | 0 (disabled) | `otaVerifyAndCommit` returns `true` unconditionally — all images accepted |
| Production / 4-universe Ethernet target | 1 (enabled, default) | Full Ed25519 verification enforced |

When the flag is absent from the build, it defaults to 1 (enabled).

## 3. State Machine

No state machine — the module is synchronous: verify-or-reject, called-and-returned.

`otaVerifyAndCommit()` produces one of two outcomes:

| Return | Meaning |
|---|---|
| `true` | Signature valid; the update partition remains the boot target. The caller (OTA subsystem) proceeds to `Update.end(true)` and `ESP.restart()`. |
| `false` | Signature invalid or any I/O/crypto error; the boot partition is rolled back to the running application. The caller sets the OTA phase to Error. |

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
5. **Build SPKI:** The 12-byte ASN.1 SubjectPublicKeyInfo prefix is concatenated
   with the 32-byte embedded public key, producing a 44-byte SPKI blob.
6. **Parse public key:** `mbedtls_pk_parse_public_key` parses the 44-byte SPKI blob
   into an mbedTLS public-key context. If parsing fails, verification returns
   `false`.
7. **Verify signature:** `mbedtls_pk_verify` is called with the mbedTLS key
   context, `MBEDTLS_MD_NONE` (indicating the 32-byte SHA-256 hash is the raw
   Ed25519 message), the 32-byte hash, and the 64-byte signature. Ed25519
   internally applies SHA-512 to the pre-hash during verification.
8. **Rollback on failure:** If verification returns non-zero, the module retrieves
   the currently running partition and sets it as the boot partition via the ESP-IDF
   OTA operations API, reversing the pending update. It then returns `false`.
9. **Success:** The update partition is left as the pending boot target. The
   function returns `true`, and the OTA subsystem finalizes the partition and
   reboots.

The host-side signing tool computes the same SHA-256 digest over the firmware bytes,
signs the 32-byte digest with Ed25519, and appends the 64-byte signature to the
image — producing the layout this module verifies.

## 5. Configuration Integration

The module reads no configuration-engine fields. The only build-time control is the
`OTA_SIGN_ENABLED` compile-time flag, which is set per build profile:

- **Dev targets** set the flag to 0 at compile time, disabling verification
  entirely.
- **Production targets** leave the flag at its default of 1, enforcing full
  Ed25519 verification on every update.

The 32-byte public key is a static constant compiled into the firmware. It is
generated by the host-side key-generation tooling and must be kept in sync with the
corresponding private key used by the host-side signing tool.

## 6. Lifecycle

- **Init:** No initialization. The public key is a static constant array; mbedTLS
  contexts are initialized and freed within each verification call.
- **Verify:** Called after streaming completes — either on the final upload chunk
  (local upload path) or after the streaming download finishes (GitHub/URL fetch
  path).
- **Commit/rollback:** The ESP-IDF partition API either leaves the update partition
  as the boot target (success ? caller does `Update.end(true)` + reboot) or sets
  the boot partition back to the running app (failure ? rollback).
- **Shutdown:** No shutdown hook — the function is called-and-returned.

## 7. Error Handling

All failures return `false`. The OTA subsystem caller sets the update phase to
Error (3) on a `false` return.

| Failure | Behavior |
|---|---|
| Update partition is NULL | Return `false` immediately |
| Image size unknown or < 64 bytes | Return `false` immediately |
| Signature read from flash fails | Return `false` |
| SHA-256 initialization fails | Free context; return `false` |
| Flash read during hashing fails | Free context; return `false` |
| SHA-256 update fails | Free context; return `false` |
| SHA-256 finalization fails | Free context; return `false` |
| SPKI public-key parsing fails | Free context; return `false` |
| Signature verification fails (non-zero return from `mbedtls_pk_verify`) | Free context; roll back boot partition to running app; return `false` |
| `OTA_SIGN_ENABLED` is 0 | Both functions return `true` unconditionally — verification skipped |

## 8. Timing Constraints

| Item | Value |
|---|---|
| SHA-256 chunk size (flash reads) | 1,024 bytes |
| Signature size | 64 bytes |
| Hash size (SHA-256) | 32 bytes |
| Public key size | 32 bytes |
| SPKI size | 44 bytes |
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
| `spki` | 44 bytes | Per verification call (stack) |
| `sig` | 64 bytes | Per verification call (stack) |
| `hash` | 32 bytes | Per verification call (stack) |
| `buf` | 1,024 bytes | Per verification call (stack) |
| `shaCtx` (mbedTLS SHA-256 context) | internal | Per verification call (stack) |
| `pk` (mbedTLS public-key context) | internal | Per verification call (stack) |
| `OTA_PUBKEY` | 32 bytes | Static const (ROM/flash) |
| `SPKI_PREFIX` | 12 bytes | Static const (ROM/flash) |

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
  builds to skip verification, but production builds must leave it enabled. The
  default when the flag is absent is 1 (enabled).
- **No partition erase on rollback:** The rollback path sets the boot partition to
  the running app but does not erase the failed update partition. Stale data may
  persist, but the ESP-IDF Update framework re-initializes the partition on the next
  `Update.begin` call.

## 11. Cross-Module Dependencies

| Module | Direction | Purpose |
|---|---|---|
| net.ota | upstream consumer | Calls `otaVerifyAndCommit()` after streaming completes (upload final chunk, or after GitHub/URL streaming) |
| net.rate-limiter | upstream guard | The OTA Rate Limiter gates the upload/streaming triggers before verification is ever reached |
| sys.firmware-version | upstream peer | The version-check task sets `updateAvailable`, which may trigger an auto-fetch (currently not wired) |
| platformio.ini | build-time | Sets `OTA_SIGN_ENABLED` per build profile (0 for dev, 1/default for production) |
| ota-key-management documentation | external | Describes key generation, embedding, signing, and rotation procedures |

## 12. Testing Verification

No unit tests or host-native tests cover the OTA Sign module. The `test/native/`
suite does not reference the signature verification code.

Verification is validated end-to-end (not unit-level) through:

- The host-side signing tool signs a firmware image; the device accepts (boot) or
  rejects (rollback) the image, validating the round-trip SHA-256 + Ed25519 path.
- The 5-minute firmware evaluation workflow: flash a signed build, monitor serial
  logs for successful boot, and confirm the web interface is reachable.
- Deliberately bad-signed images are used to validate the rollback path.

**Untested paths:**
- `otaVerifySignature` and `otaVerifyAndCommit` in isolation (no host test).
- The SHA-256 chunked hashing loop under different image sizes.
- The SPKI construction from prefix + public key.
- The partition rollback path (requires a live device).
- mbedTLS error handling paths (parse failure, verify non-zero return).

## 13. Open Questions

1. Whether the embedded 32-byte public key is a real production key or a synthetic
   placeholder. Key rotation requires recompiling all firmware with a new key
   array; no runtime key update mechanism exists.
2. Whether a future refactor plans to make `OTA_SIGN_ENABLED` a runtime-configurable
   flag rather than a compile-time constant, allowing operators to enable/disable
   verification without a rebuild.
3. Whether the mbedTLS configuration (`MBEDTLS_PK_C`, `MBEDTLS_SHA256_C`,
   `MBEDTLS_ED25519_C`) is correctly enabled at the ESP-IDF / arduino-esp32
   component level — the source calls the APIs but their build-time enablement is
   in Kconfig/sdkconfig.
4. Whether the 1,024-byte hashing chunk size is optimal for flash read performance,
   or whether a larger buffer would reduce iteration count for large images.
5. Whether the rollback path should also erase the failed update partition to
   prevent stale data from confusing a subsequent OTA.

## 14. History

- Signature verification extracted from the original monolithic OTA handler into a
  dedicated module. Functions `otaVerifySignature` and `otaVerifyAndCommit` form the
  public API.
- SPKI prefix added: the 12-byte ASN.1 SubjectPublicKeyInfo DER prefix was introduced
  to convert the raw 32-byte public key into a format consumable by
  `mbedtls_pk_parse_public_key`.
- `OTA_SIGN_ENABLED` compile-time gate added so development environments can disable
  verification. Production build profile leaves it default-enabled.
- Rollback path added: `esp_ota_set_boot_partition` to the running partition on
  verification failure, preventing a bad signature from bricking the device.
- SHA-256 chunked hashing loop (1 KB buffer) added to avoid loading the entire
  firmware image into DRAM during verification.
