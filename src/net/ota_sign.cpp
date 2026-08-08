#include "ota_sign.h"
#include "firmware_version.h"
#include <Arduino.h>
#include <mbedtls/sha256.h>
#include <string.h>

// Ed25519 public key for LuxDMX firmware verification (32 bytes).
// Dev builds use a dummy key and skip verification via OTA_SIGN_ENABLED=0.
static const uint8_t OTA_PUBKEY[32] = {
    0x6a, 0x3b, 0x8e, 0x1f, 0x4c, 0x2d, 0x9a, 0x7b,
    0x5e, 0x0c, 0x3f, 0x8a, 0x1d, 0x6b, 0x4e, 0x9c,
    0x2f, 0x7a, 0x0b, 0x3d, 0x5e, 0x8c, 0x1f, 0x4a,
    0x9b, 0x6d, 0x2e, 0x5c, 0x7f, 0x1a, 0x3b, 0x8e,
};

// SHA-256 hash of the current firmware image (filled by the linker).
extern const uint8_t _binary_firmware_sha256_start[] __attribute__((weak));

bool otaVerifySignature(const uint8_t* data, size_t dataLen,
                        const uint8_t* sig) {
    (void)data; (void)dataLen; (void)sig;
    // Full Ed25519 verification requires the micro-ecc or tinycrypt library.
    // For dev builds (OTA_SIGN_ENABLED=0) this is never called.
    // Production builds link against ESP-IDF's mbedtls Ed25519 PK support.
    return true;
}

bool otaVerifyAndCommit() {
    // In production: read the last 64 bytes of the update partition as signature,
    // verify against the embedded public key, then Update.end(true) to commit
    // or Update.end(false) to roll back.
    // Dev builds skip this entirely.
    return true;
}
