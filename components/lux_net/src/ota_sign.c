#include "ota_sign.h"
#include "ota_key.h"
#include "esp_ota_ops.h"
#include "psa/crypto.h"
#include <string.h>

#ifndef OTA_SIGN_ENABLED
#if defined(CONFIG_LUXDMX_OTA_SIGN_ENABLED)
#define OTA_SIGN_ENABLED CONFIG_LUXDMX_OTA_SIGN_ENABLED
#else
/* Production-safe default: development profiles must explicitly set 0. */
#define OTA_SIGN_ENABLED 1
#endif
#endif

static char s_last_error[48] = "idle";

static void set_error(const char *value)
{
    if (value == NULL) value = "unknown";
    strncpy(s_last_error, value, sizeof(s_last_error) - 1u);
    s_last_error[sizeof(s_last_error) - 1u] = '\0';
}

static void rollback_to_running(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running != NULL) {
        (void)esp_ota_set_boot_partition(running);
    }
}

bool otaVerifySignature(const uint8_t hash[OTA_HASH_SIZE],
                        const uint8_t signature[OTA_SIGNATURE_SIZE],
                        const uint8_t public_key[OTA_PUBLIC_KEY_SIZE])
{
#if !OTA_SIGN_ENABLED
    (void)hash;
    (void)signature;
    (void)public_key;
    set_error("disabled");
    return true;
#else
    if (hash == NULL || signature == NULL || public_key == NULL) {
        set_error("invalid argument");
        return false;
    }
    if (psa_crypto_init() != PSA_SUCCESS) {
        set_error("crypto init");
        return false;
    }

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_TWISTED_EDWARDS));
    psa_set_key_bits(&attributes, 255u);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&attributes, PSA_ALG_PURE_EDDSA);

    mbedtls_svc_key_id_t key = 0;
    psa_status_t status = psa_import_key(&attributes, public_key, OTA_PUBLIC_KEY_SIZE, &key);
    if (status == PSA_SUCCESS) {
        status = psa_verify_message(key, PSA_ALG_PURE_EDDSA, hash, OTA_HASH_SIZE,
                                    signature, OTA_SIGNATURE_SIZE);
    }
    if (key != 0) (void)psa_destroy_key(key);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS) {
        set_error("signature rejected");
        return false;
    }
    set_error("ok");
    return true;
#endif
}

bool otaVerifyImageBuffer(const uint8_t *image, size_t image_size,
                          const uint8_t public_key[OTA_PUBLIC_KEY_SIZE])
{
#if !OTA_SIGN_ENABLED
    (void)image;
    (void)image_size;
    (void)public_key;
    set_error("disabled");
    return true;
#else
    if (image == NULL || public_key == NULL || image_size < OTA_SIGNATURE_SIZE) {
        set_error("invalid image");
        return false;
    }
    uint8_t hash[OTA_HASH_SIZE] = {0};
    size_t hash_length = 0;
    if (psa_crypto_init() != PSA_SUCCESS) {
        set_error("crypto init");
        return false;
    }
    if (psa_hash_compute(PSA_ALG_SHA_256, image, image_size - OTA_SIGNATURE_SIZE,
                         hash, sizeof(hash), &hash_length) != PSA_SUCCESS ||
        hash_length != OTA_HASH_SIZE) {
        set_error("hash failed");
        return false;
    }
    return otaVerifySignature(hash, &image[image_size - OTA_SIGNATURE_SIZE], public_key);
#endif
}

bool otaVerifyPartition(const esp_partition_t *partition, size_t image_size)
{
#if !OTA_SIGN_ENABLED
    (void)partition;
    (void)image_size;
    set_error("disabled");
    return true;
#else
    if (partition == NULL || image_size < OTA_SIGNATURE_SIZE || image_size > partition->size) {
        set_error("invalid partition image");
        rollback_to_running();
        return false;
    }
    if (psa_crypto_init() != PSA_SUCCESS) {
        set_error("crypto init");
        rollback_to_running();
        return false;
    }

    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    uint8_t buffer[OTA_HASH_CHUNK_SIZE];
    uint8_t hash[OTA_HASH_SIZE];
    uint8_t signature[OTA_SIGNATURE_SIZE];
    size_t hash_length = 0;
    const size_t signed_size = image_size - OTA_SIGNATURE_SIZE;

    psa_status_t status = psa_hash_setup(&operation, PSA_ALG_SHA_256);
    for (size_t offset = 0; status == PSA_SUCCESS && offset < signed_size;) {
        size_t count = signed_size - offset;
        if (count > sizeof(buffer)) count = sizeof(buffer);
        if (esp_partition_read(partition, offset, buffer, count) != ESP_OK) {
            set_error("flash read");
            (void)psa_hash_abort(&operation);
            rollback_to_running();
            return false;
        }
        status = psa_hash_update(&operation, buffer, count);
        offset += count;
    }
    if (status != PSA_SUCCESS ||
        psa_hash_finish(&operation, hash, sizeof(hash), &hash_length) != PSA_SUCCESS ||
        hash_length != OTA_HASH_SIZE) {
        set_error("hash failed");
        (void)psa_hash_abort(&operation);
        rollback_to_running();
        return false;
    }
    if (esp_partition_read(partition, signed_size, signature, sizeof(signature)) != ESP_OK) {
        set_error("signature read");
        rollback_to_running();
        return false;
    }
    if (!otaVerifySignature(hash, signature, OTA_PUBLIC_KEY)) {
        rollback_to_running();
        return false;
    }
    set_error("ok");
    return true;
#endif
}

const char *otaVerifyLastError(void)
{
    return s_last_error;
}
