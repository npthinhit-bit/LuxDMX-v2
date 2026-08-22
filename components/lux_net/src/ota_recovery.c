#include "ota_recovery.h"
#include "logger.h"
#include "esp_ota_ops.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "ota_recovery";
/* Authoritative OTA contract: NVS namespace dmxgw, key boottry. */
static const char *NVS_NAMESPACE = "dmxgw";
static const char *NVS_ATTEMPTS_KEY = "boottry";
static uint8_t s_attempts = 0;
static bool s_pending = false;

static esp_err_t load_attempts(uint8_t *attempts)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_get_u8(handle, NVS_ATTEMPTS_KEY, attempts);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *attempts = 0;
        err = ESP_OK;
    }
    nvs_close(handle);
    return err;
}

static esp_err_t save_attempts(uint8_t attempts)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, NVS_ATTEMPTS_KEY, attempts);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    return err;
}

esp_err_t otaRecoveryInit(void)
{
    s_attempts = 0;
    s_pending = false;

    uint8_t attempts = 0;
    esp_err_t err = load_attempts(&attempts);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "failed to load boot retry counter: %s", esp_err_to_name(err));
        return err;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        LOG_ERROR(TAG, "running OTA partition unavailable");
        return ESP_ERR_INVALID_STATE;
    }

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    err = esp_ota_get_state_partition(running, &state);
    if (err == ESP_ERR_NOT_SUPPORTED || err == ESP_ERR_INVALID_STATE) {
        LOG_WARN(TAG, "boot rollback support is disabled in sdkconfig");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "failed to read OTA state: %s", esp_err_to_name(err));
        return err;
    }

    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        s_pending = true;
        /* boottry counts pending boots already attempted. */
        if (otaRecoveryShouldRollback(attempts, &s_attempts)) {
            LOG_ERROR(TAG, "boot retry limit reached (%u/%u), rolling back", attempts, OTA_BOOT_RETRY_MAX);
            (void)esp_ota_mark_app_invalid_rollback_and_reboot();
            return ESP_ERR_INVALID_STATE;
        }
        err = save_attempts(s_attempts);
        if (err != ESP_OK) {
            LOG_ERROR(TAG, "failed to persist boot retry counter: %s", esp_err_to_name(err));
            return err;
        }
        LOG_WARN(TAG, "pending OTA image boot attempt %u/%u", s_attempts, OTA_BOOT_RETRY_MAX);
    } else if (attempts != 0u) {
        /* A normal boot after a previous completed update clears stale state. */
        s_attempts = 0;
        err = save_attempts(0);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t otaRecoveryMarkHealthy(void)
{
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED && err != ESP_ERR_INVALID_STATE) {
        LOG_ERROR(TAG, "failed to mark app healthy: %s", esp_err_to_name(err));
        return err;
    }
    s_pending = false;
    s_attempts = 0;
    err = save_attempts(0);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "failed to clear boot retry counter: %s", esp_err_to_name(err));
        return err;
    }
    LOG_INFO(TAG, "application marked healthy; boot retry counter cleared");
    return ESP_OK;
}

bool otaRecoveryPending(void)
{
    return s_pending;
}

uint8_t otaRecoveryAttempts(void)
{
    return s_attempts;
}
