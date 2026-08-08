#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <Arduino.h>

// Ed25519 signature verification for OTA firmware images.
// The firmware image is signed at build time by the release script using
// `scripts/sign_ota.sh`. The 64-byte Ed25519 signature is appended to the
// binary, and the 32-byte public key is embedded in the firmware.
//
// Format:  [firmware image] [padding to 16-byte boundary] [64-byte signature]
// The last 64 bytes of the update partition are the signature.

// Verify an OTA image's Ed25519 signature against the embedded public key.
// `data` is the full image content (without the signature suffix).
// Returns true if the signature is valid.
bool otaVerifySignature(const uint8_t* data, size_t dataLen,
                        const uint8_t* sig);

// Verify and finalize an OTA update. Call after Update.end().
// Rolls back if verification fails.
bool otaVerifyAndCommit();
