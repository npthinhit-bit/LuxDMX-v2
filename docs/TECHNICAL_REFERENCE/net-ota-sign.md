# OTA Sign — Technical Reference

Domain: net.ota_sign

## 1. Domain Scope

Owns on-device verification of Ed25519 firmware signatures for OTA updates.
Loads the embedded 32-byte public key, builds an ASN.1 SubjectPublicKeyInfo (SPKI)
blob, parses it via mbedTLS, computes a SHA-256 hash of the firmware image
(stripped of its trailing 64-byte signature), and verifies the Ed25519 signature.
On failure, rolls the boot partition back to the currently running app so a
tampered or bad-signed image can never boot.

For key generation, embedding, signing, and rotation procedures, see
[docs/ota-key-management.md](../ota-key-management.md) — this module doc
covers the on-device verification path only.

Consumers:
- `src/net/ota.cpp:73` — `otaUploadChunk` calls `otaVerifyAndCommit()` when
  `OTA_SIGN_ENABLED` is set, on the `final` upload chunk.
- `src/net/ota.cpp:144` — `otaFromGitHub` calls `otaVerifyAndCommit()` after
  streaming completes, when `OTA_SIGN_ENABLED` is set.
- `tools/sign_ota.py` — the host-side counterpart that produces the
  `<firmware.bin> + <64-byte signature>` images this module verifies.

## 2. Layer Mapping

```
drv  →  cfg  →  core  →  net  →  app/sys
                         ↑
                    ota_sign.cpp: otaVerifySignature,
                                    otaVerifyAndCommit
                    └─ cross-layer: ESP-IDF OTA partition (ESP_OK)
                       via esp_partition.h / esp_ota_ops.h (ESP-IDF)
```

All code lives in the **net** layer (`src/net/ota_sign.cpp`). It crosses into
ESP-IDF partition APIs (`esp_partition_read`, `esp_ota_get_next_update_partition`,
`esp_ota_set_boot_partition`) for flash I/O.

## 3. Source Files

| File | Role |
|---|---|
| `src/net/ota_sign.cpp` | `OTA_PUBKEY[32]` (line 23), `SPKI_PREFIX[12]` (line 32), `otaVerifySignature` (line 39), `otaVerifyAndCommit` (line 63); stub fallbacks at `src/net/ota_sign.cpp:131-139` |
| `src/net/ota_sign.h` | `otaVerifySignature()` and `otaVerifyAndCommit()` declarations (lines 7-8) |
| `src/net/ota.cpp` | Consumer: calls `otaVerifyAndCommit()` at `src/net/ota.cpp:73,144`; `OTA_SIGN_ENABLED` guard at `src/net/ota.cpp:15-17` |
| `tools/sign_ota.py` | Host-side signer: SHA-256 + Ed25519, appends 64-byte signature (lines 49-53) |

## 4. Data Structures

### `OTA_PUBKEY[32]` — `static const uint8_t` (`src/net/ota_sign.cpp:23-28`)

The 32 raw bytes of the Ed25519 public key, compiled into firmware. The
comment at `src/net/ota_sign.cpp:18-22` states it MUST be generated via
`tools/gen_ota_keys.py` and kept in sync with the private key used by
`tools/sign_ota.py`.

| Byte | Value |
|---|---|
| 0–31 | `{0x6a, 0x3b, 0x8e, 0x1f, 0x4c, 0x2d, 0x9a, 0x7b, 0x5e, 0x0c, 0x3f, 0x8a, 0x1d, 0x6b, 0x4e, 0x9c, 0x2f, 0x7a, 0x0b, 0x3d, 0x5e, 0x8c, 0x1f, 0x4a, 0x9b, 0x6d, 0x2e, 0x5c, 0x7f, 0x1a, 0x3b, 0x8e}` |

### `SPKI_PREFIX[12]` — `static const uint8_t` (`src/net/ota_sign.cpp:32-37`)

ASN.1 SubjectPublicKeyInfo prefix that wraps the raw 32-byte Ed25519 key
into a structure `mbedtls_pk_parse_public_key` can consume.

| Bytes | Value | Purpose |
|---|---|---|
| 0–1 | `0x30, 0x2a` | SEQUENCE (42 bytes content) |
| 2–3 | `0x30, 0x05` | SEQUENCE (5 bytes): algorithm identifier |
| 4–8 | `0x06, 0x03, 0x2b, 0x65, 0x70` | OID 1.3.101.112 (Ed25519) |
| 9–11 | `0x03, 0x21, 0x00` | BIT STRING (33 bytes), 0 unused bits |

The full SPKI is 44 bytes = `sizeof(SPKI_PREFIX) + sizeof(OTA_PUBKEY)`
(`src/net/ota_sign.cpp:41`).

### `OTA_SIGN_ENABLED` — compile-time macro

Defaults to 1 via `#ifndef` in both `src/net/ota.cpp:15-17` and
`src/net/ota_sign.cpp:3-5`. When 0, `otaVerifyAndCommit()` returns `true`
unconditionally (`src/net/ota_sign.cpp:137-139`).

## 5. Concurrency

Single-threaded on core 0. `otaVerifyAndCommit()` and
`otaVerifySignature()` are called from:

- `otaUploadChunk` — runs on the AsyncWebServer task (core 0, priority 10
  via AsyncTCP, `src/net/web_server.cpp` registration at `src/net/web_server.cpp:64`).
- `otaFromGitHub` — runs on a dedicated core-0 task (priority 1, 8192 B stack,
  spawned at `src/net/web_routes.cpp:368-371,383-386`).

`otaVerifyAndCommit` reads from and writes to the ESP-IDF OTA partition;
`esp_partition_read` (`src/net/ota_sign.cpp:81,100`) and
`esp_ota_set_boot_partition` (`src/net/ota_sign.cpp:122`) are synchronous
flash operations. No inter-task locking is needed because verification
blocks the OTA worker until commit or rollback.

## 6. State Machine

No state machine — synchronous verify-or-reject.

`otaVerifyAndCommit()` returns `bool`:

| Return | Meaning |
|---|---|
| `true` | Signature valid; the update partition remains the boot target (caller proceeds to `Update.end(true)` + reboot) |
| `false` | Signature invalid or any I/O / crypto error; boot partition rolled back to running app (`src/net/ota_sign.cpp:120-123`) |

The caller in [net-ota](./net-ota.md) (`src/net/ota.cpp:73,144`) checks the
return and sets `otaProgPhase = 3` (`src/net/ota.cpp:74,145`) on failure.

## 7. Entry Points

| Entry point | Address | Caller |
|---|---|---|
| `otaVerifySignature(hash, sig)` | `src/net/ota_sign.cpp:39` | `otaVerifyAndCommit` (same file, line 118) |
| `otaVerifyAndCommit()` | `src/net/ota_sign.cpp:63` | `otaUploadChunk` (`src/net/ota.cpp:73`), `otaFromGitHub` (`src/net/ota.cpp:144`) |

## 8. Data Flow

1. **Locate update partition** — `esp_ota_get_next_update_partition(NULL)`
   (`src/net/ota_sign.cpp:64`); return `false` if NULL
   (`src/net/ota_sign.cpp:65-66`).
2. **Get image size** — `Update.size()` returns the total firmware image
   size set via `Update.begin()` (comment at `src/net/ota_sign.cpp:71-72`);
   reject if `UPDATE_SIZE_UNKNOWN` or `< 64` bytes
   (`src/net/ota_sign.cpp:73-74`).
3. **Extract signature** — `sigOffset = imageSize - 64`
   (`src/net/ota_sign.cpp:77`); read 64 bytes from the partition tail via
   `esp_partition_read` (`src/net/ota_sign.cpp:80-83`).
4. **Hash firmware** — SHA-256 over bytes `[0, sigOffset)`:
   - `mbedtls_sha256_init` (`src/net/ota_sign.cpp:87-88`)
   - `mbedtls_sha256_starts_ret(&shaCtx, 0)` — SHA-256 (hash size 0 = false
     flag, `src/net/ota_sign.cpp:90`)
   - Loop: `esp_partition_read` into `buf[1024]` (`src/net/ota_sign.cpp:96,100`),
     `mbedtls_sha256_update_ret` (`src/net/ota_sign.cpp:104`),
     1024-byte chunks until `sigOffset` reached
     (`src/net/ota_sign.cpp:97-109`).
   - `mbedtls_sha256_finish_ret` produces 32-byte `hash`
     (`src/net/ota_sign.cpp:111`).
5. **Build SPKI** — copy `SPKI_PREFIX` then `OTA_PUBKEY` into `spki[44]`
   (`src/net/ota_sign.cpp:41-43`).
6. **Parse key** — `mbedtls_pk_parse_public_key(&pk, spki, 44)`
   (`src/net/ota_sign.cpp:48`); return `false` on parse error
   (`src/net/ota_sign.cpp:49-51`).
7. **Verify** — `mbedtls_pk_verify(&pk, MBEDTLS_MD_NONE, hash, 32, sig, 64)`
   (`src/net/ota_sign.cpp:57`). `MBEDTLS_MD_NONE` tells mbedTLS the 32-byte
   SHA-256 hash is the raw Ed25519 message; Ed25519 internally SHA-512s the
   pre-hash during verification. Free the key context
   (`src/net/ota_sign.cpp:58`).
8. **Rollback on failure** — if verification returns non-zero
   (`src/net/ota_sign.cpp:118`), get the running partition via
   `esp_ota_get_running_partition()` (`src/net/ota_sign.cpp:120`) and set it
   as the boot partition via `esp_ota_set_boot_partition(running)`
   (`src/net/ota_sign.cpp:122`), then return `false`.

The host-side signer (`tools/sign_ota.py:49-53`) computes the same SHA-256
over the firmware bytes (`hashlib.sha256`), signs the 32-byte digest with
Ed25519 (`private_key.sign(digest)`, `sign_ota.py:51`), and appends the
64-byte signature to the firmware image (`sign_ota.py:53`).

## 9. Protocol Layout

N/A (no wire protocol). This module verifies a 64-byte Ed25519 signature
suffix appended to a raw firmware binary image. The on-flash image layout:

| Offset | Size | Field | Source |
|---|---|---|---|
| 0 | variable | Firmware image bytes (DFU/OTA) | `src/net/ota_sign.cpp:77` (`sigOffset = imageSize - 64`) |
| imageSize − 64 | 64 | Ed25519 signature (R ‖ S, 64 bytes) | `src/net/ota_sign.cpp:80-83` |

The 32-byte hash input is `SHA-256(firmware[0 .. imageSize-64])`
(`src/net/ota_sign.cpp:86-115`).

## 10. Config Integration

None directly. This module reads no `Config` fields. The `OTA_SIGN_ENABLED`
macro is a compile-time flag controlled by `platformio.ini` build_flags:

| Environment | `OTA_SIGN_ENABLED` | `platformio.ini` line |
|---|---|---|
| `esp32dev` | 0 (disabled) | `platformio.ini:73` |
| `esp32s3dev` | 0 (disabled) | `platformio.ini:95` |
| `wt32eth01` | 0 (disabled) | `platformio.ini:116` |
| `esp32s3_psram` | 0 (disabled) | `platformio.ini:155` |
| `esp32s3_n16r8_eth` | 1 (enabled, default) | `platformio.ini:167` |

When the flag is absent from the build, `#ifndef OTA_SIGN_ENABLED / #define
OTA_SIGN_ENABLED 1` in `src/net/ota_sign.cpp:3-5` (and
`src/net/ota.cpp:15-17`) defaults it to 1.

## 11. Lifecycle

- **Init**: No initialization. The public key (`OTA_PUBKEY`) is a static
  const array; mbedTLS contexts are initialized and freed within each
  verification call.
- **Verify**: Called after streaming completes (upload final chunk
  `src/net/ota.cpp:73`, or GitHub/URL stream done `src/net/ota.cpp:144`).
- **Commit/rollback**: `esp_ota_set_boot_partition` either commits the update
  partition as boot target (implicit in the `Update.end(true)` +
  `ESP.restart()` path in [net-ota](./net-ota.md), `src/net/ota.cpp:79-85`)
  or reverts to the running partition on verify failure
  (`src/net/ota_sign.cpp:120-123`).
- **Shutdown**: No shutdown hook — the function is called-and-return.

## 12. Error Handling

All failures return `false` from `otaVerifySignature` or
`otaVerifyAndCommit`. The caller sets `otaProgPhase = 3`
(`src/net/ota.cpp:74,145`).

| Failure | File:line | Behavior |
|---|---|---|
| `esp_ota_get_next_update_partition` returns NULL | `src/net/ota_sign.cpp:64-66` | Return `false` |
| `Image size unknown or < 64 bytes` | `src/net/ota_sign.cpp:72-75` | Return `false` |
| `esp_partition_read` for signature fails (`!= ESP_OK`) | `src/net/ota_sign.cpp:81-83` | Return `false` |
| `mbedtls_sha256_starts_ret` fails | `src/net/ota_sign.cpp:90-93` | Free context; return `false` |
| `esp_partition_read` during hash fails | `src/net/ota_sign.cpp:100-103` | Free context; return `false` |
| `mbedtls_sha256_update_ret` fails | `src/net/ota_sign.cpp:104-107` | Free context; return `false` |
| `mbedtls_sha256_finish_ret` fails | `src/net/ota_sign.cpp:111-114` | Free context; return `false` |
| `mbedtls_pk_parse_public_key` parse fails | `src/net/ota_sign.cpp:49-52` | Free context; return `false` |
| `mbedtls_pk_verify` returns non-zero | `src/net/ota_sign.cpp:57-60` | Free context; return `false` |
| Final verification fails → **rollback** | `src/net/ota_sign.cpp:120-123` | `esp_ota_get_running_partition` + `esp_ota_set_boot_partition(running)`; return `false` |
| `OTA_SIGN_ENABLED == 0` | `src/net/ota_sign.cpp:129-140` | Both functions return `true` unconditionally — verification skipped |

## 13. Memory Allocation

- `spki[44]` (`src/net/ota_sign.cpp:41`) — stack buffer: `sizeof(SPKI_PREFIX) + sizeof(OTA_PUBKEY)` = 12 + 32 = 44 bytes.
- `sig[64]` (`src/net/ota_sign.cpp:80`) — stack buffer for the signature.
- `hash[32]` (`src/net/ota_sign.cpp:86`) — stack buffer for the SHA-256 digest.
- `buf[1024]` (`src/net/ota_sign.cpp:96`) — stack buffer for chunked flash reads during hashing.
- `mbedtls_sha256_context shaCtx` (`src/net/ota_sign.cpp:87`) — mbedTLS internal state, stack.
- `mbedtls_pk_context pk` (`src/net/ota_sign.cpp:46`) — mbedTLS internal state, stack.
- `OTA_PUBKEY[32]` and `SPKI_PREFIX[12]` (`src/net/ota_sign.cpp:23,32`) — `static const` in ROM/flash.

No heap allocation. No PSRAM usage.

## 14. Timing

| Item | Value | Source |
|---|---|---|
| SHA-256 chunk size for flash reads | 1024 bytes | `src/net/ota_sign.cpp:96` |
| Signature size | 64 bytes | `src/net/ota_sign.cpp:77,80` |
| Hash size (SHA-256) | 32 bytes | `src/net/ota_sign.cpp:57,86` |
| Public key size | 32 bytes | `src/net/ota_sign.cpp:23,41` |
| SPKI size | 44 bytes | `src/net/ota_sign.cpp:41` (12 + 32) |
| Minimum image size for verification | 64 bytes | `src/net/ota_sign.cpp:73` |

The hashing loop reads 1 KB at a time from flash via `esp_partition_read`
(`src/net/ota_sign.cpp:97-109`); duration scales linearly with firmware image
size. For a ~500 KB image, ~500 iterations of read+update+SHA-256.
No hard deadline — runs to completion on the core-0 OTA task.

## 15. Traceability

| Claim | File:line |
|---|---|
| `OTA_PUBKEY` is a 32-byte static const array | `src/net/ota_sign.cpp:23-28` |
| `SPKI_PREFIX` is a 12-byte ASN.1 DER prefix | `src/net/ota_sign.cpp:32-37` |
| `OTA_SIGN_ENABLED` defaults to 1 in ota_sign.cpp | `src/net/ota_sign.cpp:3-5` |
| `OTA_SIGN_ENABLED` defaults to 1 in ota.cpp | `src/net/ota.cpp:15-17` |
| `otaVerifySignature` builds 44-byte SPKI from prefix + key | `src/net/ota_sign.cpp:41-43` |
| `otaVerifySignature` parses SPKI via `mbedtls_pk_parse_public_key` | `src/net/ota_sign.cpp:48` |
| `otaVerifySignature` calls `mbedtls_pk_verify` with `MBEDTLS_MD_NONE`, 32-byte hash, 64-byte sig | `src/net/ota_sign.cpp:57` |
| `otaVerifyAndCommit` gets update partition | `src/net/ota_sign.cpp:64` |
| `otaVerifyAndCommit` rejects if `imageSize < 64` | `src/net/ota_sign.cpp:73-74` |
| `otaVerifyAndCommit` reads 64-byte signature from `imageSize - 64` | `src/net/ota_sign.cpp:77,80-83` |
| `otaVerifyAndCommit` inits SHA-256 with `starts_ret(ctx, 0)` (SHA-256, not 384/512) | `src/net/ota_sign.cpp:90` |
| `otaVerifyAndCommit` reads flash in 1024-byte chunks | `src/net/ota_sign.cpp:96,98-100` |
| `otaVerifyAndCommit` finalizes SHA-256 → 32-byte hash | `src/net/ota_sign.cpp:111` |
| `otaVerifyAndCommit` calls `otaVerifySignature(hash, sig)` | `src/net/ota_sign.cpp:118` |
| `otaVerifyAndCommit` rollback: `esp_ota_get_running_partition` + `esp_ota_set_boot_partition` | `src/net/ota_sign.cpp:120-123` |
| `OTA_SIGN_ENABLED == 0` stub: `otaVerifySignature` returns `true` | `src/net/ota_sign.cpp:131-135` |
| `OTA_SIGN_ENABLED == 0` stub: `otaVerifyAndCommit` returns `true` | `src/net/ota_sign.cpp:137-139` |
| `otaVerifyAndCommit` called from `otaUploadChunk` on `final` chunk | `src/net/ota.cpp:73` |
| `otaVerifyAndCommit` called from `otaFromGitHub` after stream | `src/net/ota.cpp:144` |
| `ota.cpp` gates both calls with `#if OTA_SIGN_ENABLED` | `src/net/ota.cpp:72-78,143-148` |
| Rate limiter gate in `otaUploadChunk` at `index==0` | `src/net/ota.cpp:48` |
| `/ota/github` and `/ota/url` routes are rate-limited | `src/net/web_server.cpp:56-61` |
| `handleOtaGithub` spawns core-0 task | `src/net/web_routes.cpp:368-371` |
| `handleOtaUrl` spawns core-0 task | `src/net/web_routes.cpp:383-386` |
| Host-side signer computes SHA-256 | `tools/sign_ota.py:49` |
| Host-side signer signs digest with Ed25519 | `tools/sign_ota.py:51` |
| Host-side signer appends 64-byte signature | `tools/sign_ota.py:53` |
| `OTA_BOOT_TRIES` = 3 (boot guard cap) | `src/sys/sys_platform.h:15` |
| `otaProgPhase` declared extern | `src/sys/sys_platform.h:30` |
| ESP-IDF partition APIs: `esp_partition_read`, `esp_ota_get_next_update_partition`, `esp_ota_set_boot_partition` | `src/net/ota_sign.cpp:64,81,100,120,122` |
| mbedTLS APIs: `mbedtls_pk_parse_public_key`, `mbedtls_pk_verify`, `mbedtls_sha256_*` | `src/net/ota_sign.cpp:48,57,88,90,104,111` |

## 16. Cross-References

- [net-ota](./net-ota.md) — consumer of `otaVerifyAndCommit`
  (`src/net/ota.cpp:73,144`); `otaProgPhase` set to 3 on failure
  (`src/net/ota.cpp:74,145`).
- [net-rate-limiter](./net-rate-limiter.md) — `g_otaRateLimiter` gate sits
  in the caller `otaUploadChunk` (`src/net/ota.cpp:48`), before verification
  is ever reached.
- [sys-tasks](./sys-tasks.md) — `versionCheckTask`
  (`src/sys/tasks.cpp:172-176`) runs `versionCheck()` which may set
  `updateAvailable` (`src/sys/firmware_version.cpp:73`).
- [docs/ota-key-management.md](../ota-key-management.md) — key generation
  (`tools/gen_ota_keys.py`), embedding into `OTA_PUBKEY`
  (`src/net/ota_sign.cpp:23-28`), signing with `tools/sign_ota.py`
  (`src/net/ota_sign.cpp:18-22` comment), and rotation procedure.
- [config-engine](./config-engine.md) — `OTA_SIGN_ENABLED` is a
  `platformio.ini` build flag, not a `Config` field; see
  `platformio.ini:73,95,116,155,167`.

## 17. Limitations

- The embedded `OTA_PUBKEY` (`src/net/ota_sign.cpp:23-28`) is a hardcoded
  placeholder — it is a synthetic demo key, not a real production key.
  Key rotation requires recompiling all firmware with a new array
  (`src/net/ota_sign.cpp:18-22` comment).
- SHA-256 is computed in 1024-byte chunks from flash
  (`src/net/ota_sign.cpp:96-109`); for large images (~500 KB), this takes
  proportionally long with no progress callback — the web UI cannot show
  verification progress separately from download progress.
- `MBEDTLS_MD_NONE` is passed to `mbedtls_pk_verify`
  (`src/net/ota_sign.cpp:57`) — this is correct for pure Ed25519 (the 32-byte
  SHA-256 digest is the message), but it relies on mbedTLS Ed25519 internals
  SHA-512 hashing the pre-hash; any future mbedTLS version that changes this
  semantics would silently break verification.
- When `OTA_SIGN_ENABLED == 0`, both `otaVerifyAndCommit` and
  `otaVerifySignature` return `true` unconditionally
  (`src/net/ota_sign.cpp:131-139`) — all dev environments disable signing
  (`platformio.ini:73,95,116,155`).
- No signature verification of the partition table itself — only the app
  image is signed; the OTA data partition and bootloader are trusted by
  construction.
- The rollback path (`src/net/ota_sign.cpp:120-123`) sets the boot partition
  back to the running app, but does **not** erase the failed update
  partition — stale data may persist and confuse a subsequent OTA.

## 18. Open Questions

- Not determinable from the inspected source code — whether
  `tools/gen_ota_keys.py` exists and is consistent with the
  `OTA_PUBKEY` array; the comment at `src/net/ota_sign.cpp:18-22` references
  it, but the script was not in the inspected file set.
- Not determinable from the inspected source code — whether a future refactor
  plans to move `OTA_BOOT_TRIES` from `sys_platform.h` into this module's
  domain, since the boot-guard logic in `ota.cpp:19-41` is in the OTA module
  but the constant lives in sys.
- Not determinable from the inspected source code — the exact mbedTLS
  configuration (`MBEDTLS_PK_C`, `MBEDTLS_SHA256_C`, `MBEDTLS_ED25519_C`)
  needed at the ESP-IDF / arduino-esp32 component level; the source calls the
  APIs but their build-time enablement is in `Kconfig`/`sdkconfig`, not
  inspected here.

## 19. Testing

No unit test or native test coverage for the OTA signature module. The
`test/native/` suite does not reference `ota_sign.cpp` or `sign_ota.py`.

The signature flow is validated end-to-end by the firmware evaluation workflow
documented in `CLAUDE.md` (sign with `tools/sign_ota.py`, flash, verify boot
partition), but no automated test exercises `otaVerifyAndCommit` or
`otaVerifySignature` on the host.

## 20. History

- Signature verification extracted from the original monolith OTA handler
  into `src/net/ota_sign.cpp` (`otaVerifySignature` at line 39,
  `otaVerifyAndCommit` at line 63).
- SPKI prefix (`SPKI_PREFIX[12]`, `src/net/ota_sign.cpp:32-37`) added to
  convert the raw 32-byte `OTA_PUBKEY` into a format consumable by
  `mbedtls_pk_parse_public_key` (`src/net/ota_sign.cpp:48`).
- `OTA_SIGN_ENABLED` compile-time gate added so dev environments can disable
  verification (`platformio.ini:73,95,116,155` set it to 0; production
  `esp32s3_n16r8_eth` at `platformio.ini:167` leaves it default-enabled).
- Rollback path (`esp_ota_set_boot_partition` to running partition) added at
  `src/net/ota_sign.cpp:120-123` to prevent a bad signature from bricking the
  device.
- SHA-256 chunked hashing loop (`buf[1024]`,
  `src/net/ota_sign.cpp:96-109`) added to avoid loading the entire firmware
  into DRAM during verification.
