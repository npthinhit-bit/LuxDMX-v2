#ifndef LUXDMX_OTA_SIGN_H
#define LUXDMX_OTA_SIGN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_partition.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_PUBLIC_KEY_SIZE 32u
#define OTA_SIGNATURE_SIZE 64u
#define OTA_HASH_SIZE 32u
#define OTA_HASH_CHUNK_SIZE 1024u

/* Verify an Ed25519 signature over the SHA-256 digest supplied by the OTA worker. */
bool otaVerifySignature(const uint8_t hash[OTA_HASH_SIZE],
                        const uint8_t signature[OTA_SIGNATURE_SIZE],
                        const uint8_t public_key[OTA_PUBLIC_KEY_SIZE]);

/* Verify [image bytes || 64-byte signature] without retaining image storage. */
bool otaVerifyImageBuffer(const uint8_t *image, size_t image_size,
                          const uint8_t public_key[OTA_PUBLIC_KEY_SIZE]);

/*
 * Verify an image already streamed to an ESP-IDF OTA partition. The final
 * 64 bytes are the Ed25519 signature and are excluded from the SHA-256 input.
 * This function never selects a boot target; the caller commits it only after
 * this returns true. On failure, the running partition is restored explicitly.
 */
bool otaVerifyPartition(const esp_partition_t *partition, size_t image_size);

/* Stable diagnostic string for the most recent verification attempt. */
const char *otaVerifyLastError(void);

#ifdef __cplusplus
}
#endif

#endif
