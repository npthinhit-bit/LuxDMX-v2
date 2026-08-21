#include "ota_sign.h"

#include "psa/crypto.h"

#ifndef OTA_SIGN_ENABLED
#define OTA_SIGN_ENABLED 0
#endif

bool otaVerifySignature(const uint8_t hash[OTA_HASH_SIZE],
                        const uint8_t signature[OTA_SIGNATURE_SIZE],
                        const uint8_t public_key[OTA_PUBLIC_KEY_SIZE])
{
#if !OTA_SIGN_ENABLED
    (void)hash;
    (void)signature;
    (void)public_key;
    return true;
#else
    if (hash == NULL || signature == NULL || public_key == NULL) return false;
    if (psa_crypto_init() != PSA_SUCCESS) return false;

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
    if (key != 0) psa_destroy_key(key);
    psa_reset_key_attributes(&attributes);
    return status == PSA_SUCCESS;
#endif
}

bool otaVerifyImageBuffer(const uint8_t *image, size_t image_size,
                          const uint8_t public_key[OTA_PUBLIC_KEY_SIZE])
{
#if !OTA_SIGN_ENABLED
    (void)image;
    (void)image_size;
    (void)public_key;
    return true;
#else
    if (image == NULL || public_key == NULL || image_size < OTA_SIGNATURE_SIZE) return false;
    uint8_t hash[OTA_HASH_SIZE] = {0};
    size_t hash_length = 0;
    if (psa_crypto_init() != PSA_SUCCESS) return false;
    if (psa_hash_compute(PSA_ALG_SHA_256, image, image_size - OTA_SIGNATURE_SIZE,
                         hash, sizeof(hash), &hash_length) != PSA_SUCCESS ||
        hash_length != OTA_HASH_SIZE) {
        return false;
    }
    return otaVerifySignature(hash, &image[image_size - OTA_SIGNATURE_SIZE], public_key);
#endif
}
