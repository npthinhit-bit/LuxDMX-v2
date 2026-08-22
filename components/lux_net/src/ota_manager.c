#include "ota_manager.h"
#include "logger.h"
#include "ota_sign.h"
#include "ota_recovery.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ota_manager";
static volatile ota_phase_t s_phase = OTA_PHASE_IDLE;
static volatile uint8_t s_progress = 0;
static char s_target[96] = "local upload";
static esp_ota_handle_t s_handle = 0;
static const esp_partition_t *s_partition = NULL;
static uint32_t s_written = 0;
static char s_error[96] = "";

/* Five requests/minute with a burst capacity of ten, shared by OTA endpoints. */
static float s_tokens = 10.0f;
static int64_t s_last_refill_us = 0;

static bool ota_rate_allow(void)
{
    const int64_t now = esp_timer_get_time();
    if (s_last_refill_us == 0) s_last_refill_us = now;
    const int64_t elapsed = now - s_last_refill_us;
    s_tokens += (float)elapsed * 5.0f / 60000000.0f;
    if (s_tokens > 10.0f) s_tokens = 10.0f;
    s_last_refill_us = now;
    if (s_tokens < 1.0f) return false;
    s_tokens -= 1.0f;
    return true;
}

static void ota_restore_running_boot(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running != NULL) {
        (void)esp_ota_set_boot_partition(running);
    }
}

static void ota_fail(void)
{
    if (s_handle != 0) {
        esp_ota_abort(s_handle);
        s_handle = 0;
    }
    s_partition = NULL;
    s_written = 0;
    s_progress = 0;
    if (s_error[0] == '\0') {
        strncpy(s_error, "OTA operation failed", sizeof(s_error) - 1u);
        s_error[sizeof(s_error) - 1u] = '\0';
    }
    s_phase = OTA_PHASE_ERROR;
}

static esp_err_t ota_status_handler(httpd_req_t *req)
{
    char json[192];
    int length = snprintf(json, sizeof(json),
                          "{\"phase\":%u,\"pct\":%u,\"target\":\"%s\",\"error\":\"%s\",\"bootRetry\":%u}",
                          (unsigned)s_phase, (unsigned)s_progress, s_target, s_error,
                          (unsigned)otaRecoveryAttempts());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, length);
}

static esp_err_t ota_upload_handler(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > 0xFFFFFFFFu) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware image size is invalid");
        return ESP_FAIL;
    }
    if (!ota_rate_allow()) {
        httpd_resp_set_hdr(req, "Retry-After", "60");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_set_status(req, "429 Too Many Requests");
        httpd_resp_send(req, "OTA rate limit exceeded", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    s_phase = OTA_PHASE_WRITING;
    s_progress = 0;
    s_written = 0;
    s_error[0] = '\0';
    strncpy(s_target, "local upload", sizeof(s_target) - 1u);
    s_target[sizeof(s_target) - 1u] = '\0';
    s_partition = esp_ota_get_next_update_partition(NULL);
    if (s_partition == NULL || req->content_len > s_partition->size) {
        ota_fail();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition available");
        return ESP_FAIL;
    }

    esp_err_t err = esp_ota_begin(s_partition, req->content_len, &s_handle);
    if (err != ESP_OK) {
        ota_fail();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return err;
    }

    uint8_t buffer[1024];
    while (s_written < req->content_len) {
        size_t remaining = req->content_len - s_written;
        size_t request_size = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        int received = httpd_req_recv(req, (char *)buffer, request_size);
        if (received <= 0 || esp_ota_write(s_handle, buffer, (size_t)received) != ESP_OK) {
            ota_fail();
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }
        s_written += (uint32_t)received;
        s_progress = (uint8_t)(((uint64_t)s_written * 100u) / req->content_len);
    }

    if (esp_ota_end(s_handle) != ESP_OK) {
        ota_fail();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA finalize failed");
        return ESP_FAIL;
    }
    s_handle = 0;
    s_phase = OTA_PHASE_VERIFYING;
    if (!otaVerifyPartition(s_partition, s_written)) {
        strncpy(s_error, otaVerifyLastError(), sizeof(s_error) - 1u);
        s_error[sizeof(s_error) - 1u] = '\0';
        ota_fail();
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Firmware signature verification failed");
        return ESP_FAIL;
    }
    if (esp_ota_set_boot_partition(s_partition) != ESP_OK) {
        ota_restore_running_boot();
        strncpy(s_error, "boot target failed", sizeof(s_error) - 1u);
        s_error[sizeof(s_error) - 1u] = '\0';
        ota_fail();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA boot target failed");
        return ESP_FAIL;
    }
    s_partition = NULL;
    s_phase = OTA_PHASE_FINALIZING;
    s_progress = 100;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t response_err = httpd_resp_send(req, "{\"ok\":true,\"rebooting\":true}", HTTPD_RESP_USE_STRLEN);
    if (response_err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    }
    return response_err;
}

void otaManagerInit(void)
{
    s_phase = OTA_PHASE_IDLE;
    s_progress = 0;
    s_written = 0;
    s_handle = 0;
    s_partition = NULL;
    s_error[0] = '\0';
    s_last_refill_us = esp_timer_get_time();
    s_tokens = 10.0f;
}

esp_err_t otaManagerRegister(httpd_handle_t server)
{
    static const httpd_uri_t status_uri = {
        .uri = "/ota/status",
        .method = HTTP_GET,
        .handler = ota_status_handler,
        .user_ctx = NULL,
    };
    static const httpd_uri_t upload_uri = {
        .uri = "/ota/upload",
        .method = HTTP_POST,
        .handler = ota_upload_handler,
        .user_ctx = NULL,
    };
    esp_err_t err = httpd_register_uri_handler(server, &status_uri);
    if (err != ESP_OK) return err;
    err = httpd_register_uri_handler(server, &upload_uri);
    if (err != ESP_OK) return err;
    LOG_INFO(TAG, "OTA upload/status endpoints registered");
    return ESP_OK;
}

ota_phase_t otaManagerPhase(void) { return s_phase; }
uint8_t otaManagerProgress(void) { return s_progress; }
const char *otaManagerTarget(void) { return s_target; }
const char *otaManagerError(void) { return s_error; }
