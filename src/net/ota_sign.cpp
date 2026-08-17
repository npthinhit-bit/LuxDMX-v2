#include "ota_sign.h"

#ifndef OTA_SIGN_ENABLED
#define OTA_SIGN_ENABLED 1
#endif

#if OTA_SIGN_ENABLED

#include <Arduino.h>
#include <Update.h>
#include <string.h>
#include <mbedtls/sha256.h>
#include <mbedtls/pk.h>
#include <mbedtls/md.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

// Ed25519 public key for LuxDMX firmware verification (32 raw bytes).
// This key MUST be generated via tools/gen_ota_keys.py and kept in sync
// with the private key used by tools/sign_ota.py. To rotate, generate a
// new pair, update this array, recompile, then re-sign all firmware
// images with the new private key.
static const uint8_t OTA_PUBKEY[32] = {
    0x6a, 0x3b, 0x8e, 0x1f, 0x4c, 0x2d, 0x9a, 0x7b,
    0x5e, 0x0c, 0x3f, 0x8a, 0x1d, 0x6b, 0x4e, 0x9c,
    0x2f, 0x7a, 0x0b, 0x3d, 0x5e, 0x8c, 0x1f, 0x4a,
    0x9b, 0x6d, 0x2e, 0x5c, 0x7f, 0x1a, 0x3b, 0x8e,
};

// ASN.1 SubjectPublicKeyInfo prefix for an Ed25519 public key.
// SEQUENCE { SEQUENCE { OID 1.3.101.112 (Ed25519) }, BIT STRING { 0x00 <key> } }
static const uint8_t SPKI_PREFIX[12] = {
    0x30, 0x2a,                         // SEQUENCE (42 bytes content)
    0x30, 0x05,                         // SEQUENCE (5 bytes): algorithm identifier
    0x06, 0x03, 0x2b, 0x65, 0x70,       // OID 1.3.101.112 (Ed25519)
    0x03, 0x21, 0x00,                   // BIT STRING (33 bytes), 0 unused bits
};

bool otaVerifySignature(const uint8_t* hash, const uint8_t* sig) {
    // Build the ASN.1 SubjectPublicKeyInfo from the raw 32-byte key.
    uint8_t spki[sizeof(SPKI_PREFIX) + sizeof(OTA_PUBKEY)];
    memcpy(spki, SPKI_PREFIX, sizeof(SPKI_PREFIX));
    memcpy(spki + sizeof(SPKI_PREFIX), OTA_PUBKEY, sizeof(OTA_PUBKEY));

    // Parse the SPKI into an mbedtls public-key context.
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int ret = mbedtls_pk_parse_public_key(&pk, spki, sizeof(spki));
    if (ret != 0) {
        mbedtls_pk_free(&pk);
        return false;
    }

    // Verify the 64-byte Ed25519 signature against the 32-byte SHA-256 hash.
    // MBEDTLS_MD_NONE tells mbedtls to use the provided hash directly
    // (Ed25519 internally SHA-512s the 32-byte pre-hash during verification).
    ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_NONE, hash, 32, sig, 64);
    mbedtls_pk_free(&pk);

    return (ret == 0);
}

bool otaVerifyAndCommit() {
    const esp_partition_t* updatePart = esp_ota_get_next_update_partition(NULL);
    if (!updatePart) {
        return false;
    }

    // The firmware image (including the 64-byte signature suffix) has been
    // written to the update partition by the Update class.  Update.size()
    // returns the total firmware image size set via Update.begin().
    uint32_t imageSize = Update.size();
    if (imageSize == UPDATE_SIZE_UNKNOWN || imageSize < 64) {
        return false;
    }

    size_t sigOffset = imageSize - 64;

    // Read the 64-byte signature from the end of the firmware image.
    uint8_t sig[64];
    if (esp_partition_read(updatePart, sigOffset, sig, sizeof(sig)) != ESP_OK) {
        return false;
    }

    // Compute SHA-256 of the firmware image (everything before the signature).
    uint8_t hash[32];
    mbedtls_sha256_context shaCtx;
    mbedtls_sha256_init(&shaCtx);

    if (mbedtls_sha256_starts(&shaCtx, 0) != 0) {
        mbedtls_sha256_free(&shaCtx);
        return false;
    }

    size_t offset = 0;
    uint8_t buf[1024];
    while (offset < sigOffset) {
        size_t remaining = sigOffset - offset;
        size_t toRead = sizeof(buf) < remaining ? sizeof(buf) : remaining;
        if (esp_partition_read(updatePart, offset, buf, toRead) != ESP_OK) {
            mbedtls_sha256_free(&shaCtx);
            return false;
        }
        if (mbedtls_sha256_update(&shaCtx, buf, toRead) != 0) {
            mbedtls_sha256_free(&shaCtx);
            return false;
        }
        offset += toRead;
    }

    if (mbedtls_sha256_finish(&shaCtx, hash) != 0) {
        mbedtls_sha256_free(&shaCtx);
        return false;
    }
    mbedtls_sha256_free(&shaCtx);

    // Verify the Ed25519 signature against the SHA-256 hash.
    if (!otaVerifySignature(hash, sig)) {
        // Rollback: switch boot partition back to the currently running app.
        const esp_partition_t* running = esp_ota_get_running_partition();
        if (running) {
            esp_ota_set_boot_partition(running);
        }
        return false;
    }
    return true;
}

#else  // OTA_SIGN_ENABLED == 0

bool otaVerifySignature(const uint8_t* hash, const uint8_t* sig) {
    (void)hash;
    (void)sig;
    return true;
}

bool otaVerifyAndCommit() {
    return true;
}

#endif