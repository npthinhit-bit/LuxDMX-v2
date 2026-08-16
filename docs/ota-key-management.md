# OTA Key Management

This document describes the Ed25519 signature flow used to secure firmware
over-the-air (OTA) updates on LuxDMX gateways, and how to generate keys,
embed the public key, sign firmware, and rotate keys.

## 1. Overview

Each released firmware image is signed with an Ed25519 private key. The
gateway stores the corresponding **public key** compiled into
`src/net/ota_sign.cpp`. During an OTA update the firmware image is written
to the update partition with a 64-byte Ed25519 signature appended to the
end of the image.

Verification flow (on-device, `src/net/ota_sign.cpp`):

1. The update partition contains `<firmware bytes> + <64-byte signature>`.
2. `otaVerifyAndCommit()` computes the **SHA-256** hash of the firmware bytes
   (everything before the signature suffix).
3. The 64-byte signature is read from the tail of the partition.
4. `otaVerifySignature()` builds an ASN.1 SubjectPublicKeyInfo (SPKI) blob from
   the embedded 32-byte raw public key and parses it with mbedTLS
   `mbedtls_pk_parse_public_key()`.
5. `mbedtls_pk_verify()` is called with `MBEDTLS_MD_NONE` — this tells
   mbedTLS to use the provided 32-byte hash directly as the Ed25519 message.
   (Internally Ed25519 SHA-512s the pre-hash before signing/verifying.)
6. If verification succeeds, the partition table is updated to boot from the
   new image. If it fails, the boot partition is set back to the currently
   running app (rollback) and the update is rejected.

The signing flow (host-side, `tools/sign_ota.py`):

1. Read the firmware `.bin` file.
2. Compute its SHA-256 hash (32 bytes).
3. Sign the hash directly with Ed25519 (pure Ed25519 — the hash is the
   "message" to sign, no pre-hash wrapping).
4. Append the 64-byte signature to the firmware bytes.
5. Write the combined image to the output file.

## Signing is controlled by `OTA_SIGN_ENABLED`

In `platformio.ini`, each environment that supports signed OTA builds defines
`-DOTA_SIGN_ENABLED=0` (disabled) or omits the flag (defaults to enabled via
`#ifndef OTA_SIGN_ENABLED / #define OTA_SIGN_ENABLED 1` in `ota_sign.cpp`).
When disabled, `otaVerifyAndCommit()` returns `true` unconditionally — every
update is accepted.

## 2. Generating a Key Pair

Install the required library and generate a new key pair:

```bash
pip install cryptography
python3 tools/gen_ota_keys.py
```

This produces two files in `tools/`:

| File                | Description                                      |
|---------------------|--------------------------------------------------|
| `tools/ota_private.pem` | PEM-encoded Ed25519 private key (PKCS#8).   |
| `tools/ota_public.bin`  | Raw 32-byte Ed25519 public key.             |

The script also prints a C array initializer to stdout. Copy that output
into `src/net/ota_sign.cpp` to replace the `OTA_PUBKEY` array (see below).

## 3. Embedding the Public Key

After generating a key pair, embed the public key in `src/net/ota_sign.cpp`.
The generated script output looks like:

```c
static const uint8_t OTA_PUBKEY[32] = {
    0x6a, 0x3b, 0x8e, 0x1f, 0x4c, 0x2d, 0x9a, 0x7b,
    0x5e, 0x0c, 0x3f, 0x8a, 0x1d, 0x6b, 0x4e, 0x9c,
    0x2f, 0x7a, 0x0b, 0x3d, 0x5e, 0x8c, 0x1f, 0x4a,
    0x9b, 0x6d, 0x2e, 0x5c, 0x7f, 0x1a, 0x3b, 0x8e,
};
```

Replace the existing `OTA_PUBKEY` array with the output from
`tools/gen_ota_keys.py`, then recompile the firmware. Devices running the
newly compiled firmware will accept updates signed with the matching private
key.

## 4. Signing Firmware

To sign a firmware image for distribution:

```bash
python3 tools/sign_ota.py <firmware.bin> <private_key.pem> <output.bin>
```

For example:

```bash
pio run -e esp32s3_psram
python3 tools/sign_ota.py .pio/build/esp32s3_psram/firmware.bin \
    tools/ota_private.pem releases/luxdmx-v1.2.3-signed.bin
```

The output file (`output.bin`) is the original firmware with the 64-byte
signature appended. Upload this signed image via the web UI (`/ota/upload`),
GitHub release endpoint (`/ota/github`), or URL fetch endpoint (`/ota/url`).

The script prints the SHA-256 hash and signature hex for verification:

```
SHA-256:   a1b2c3d4...
Signature: e5f6a7b8...
```

You can independently verify the signature against the public key using
`openssl` or the `cryptography` library.

## 5. Key Rotation Procedure

To rotate the signing key:

1. **Generate a new key pair:**
   ```bash
   python3 tools/gen_ota_keys.py
   ```
   This overwrites `tools/ota_private.pem` and `tools/ota_public.bin`.

2. **Archive the old private key** (see Security Considerations below).

3. **Embed the new public key** in `src/net/ota_sign.cpp` (copy the C array
   initializer printed by the script).

4. **Recompile and flash** the firmware to all devices with the new public
   key. Until devices run the updated firmware, they will only accept images
   signed with the **old** key.

5. **Re-sign all firmware** with the new private key using
   `tools/sign_ota.py`.

6. **Distribute** the newly signed firmware. Devices running the updated
   firmware will verify updates against the new key.

Key rotation should be infrequent — ideally annually or after a suspected
key compromise. Coordinate distribution so that devices receive the new
public key before distributing signed updates.

## 6. Security Considerations

- **Never commit the private key to version control.** The
  `tools/ota_private.pem` file (and any signing key you generate) must
  remain secret. Add it to `.gitignore`:

  ```
  tools/ota_private.pem
  ```

- **Store the private key offline.** Keep the private key on an air-gapped
  machine or hardware security module (HSM). Only move signed firmware images
  off the signing host.

- **The public key is embedded in firmware** and is not secret. Anyone can
  read `OTA_PUBKEY` from the binary. The security model relies entirely on
  the private key remaining uncompromised.

- **SHA-256 is computed on-device** over the firmware bytes in the update
  partition. The hash itself is not secret and does not need to be
  protected.

- **Pure Ed25519.** The 32-byte SHA-256 digest is signed directly as the
  Ed25519 message. This is the standard approach for firmware signing and is
  collision-resistant as long as SHA-256 is secure.

- **Rollback protection.** When a signature verification fails, the device
  reverts the boot partition to the currently running firmware. This
  prevents an attacker from downgrading to a vulnerable version via a
  tampered update.

- **Disable signing in development.** When `OTA_SIGN_ENABLED=0`, signature
  verification is skipped entirely. Never ship production firmware with
  signing disabled.

- **Rotate keys periodically.** Even if you do not suspect a compromise,
  annual key rotation limits the blast radius of a discovered vulnerability.