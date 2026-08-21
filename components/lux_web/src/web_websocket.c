#include "web_websocket.h"
#include "logger.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "web_websocket";
static httpd_handle_t ws_server = NULL;
static int ws_fd = -1;

#if CONFIG_HTTPD_WS_SUPPORT
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ws_fd = httpd_req_to_sockfd(req);
        LOG_INFO(TAG, "WebSocket client connected fd=%d", ws_fd);
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {0};
    frame.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) return err;
    if (frame.len == 0 || frame.len > 255u) return ESP_ERR_INVALID_SIZE;

    uint8_t payload[256] = {0};
    frame.payload = payload;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK) return err;

    static const char response[] = "{\"ok\":true}";
    httpd_ws_frame_t reply = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)response,
        .len = sizeof(response) - 1u,
    };
    return httpd_ws_send_frame(req, &reply);
}
#else
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ws_fd = httpd_req_to_sockfd(req);
        LOG_WARN(TAG, "WebSocket support disabled in sdkconfig; HTTP upgrade is unavailable");
        httpd_resp_set_status(req, "426 Upgrade Required");
        return httpd_resp_send(req, "WebSocket support disabled", HTTPD_RESP_USE_STRLEN);
    }
    return ESP_ERR_NOT_SUPPORTED;
}
#endif

esp_err_t web_websocket_init(httpd_handle_t server)
{
    ws_server = server;
    static const httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
#if CONFIG_HTTPD_WS_SUPPORT
        .is_websocket = true,
#endif
    };
    esp_err_t err = httpd_register_uri_handler(server, &ws_uri);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to register WebSocket handler: %s", esp_err_to_name(err));
        return err;
    }
    LOG_INFO(TAG, "WebSocket initialized");
    return ESP_OK;
}

esp_err_t web_websocket_send_status(void)
{
#if CONFIG_HTTPD_WS_SUPPORT
    if (ws_server == NULL || ws_fd < 0) return ESP_ERR_INVALID_STATE;
    static const char status[] = "{\"meta\":1,\"state\":\"station\",\"senders\":[],\"log\":[]}";
    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)status,
        .len = sizeof(status) - 1u,
    };
    return httpd_ws_send_frame_async(ws_server, ws_fd, &frame);
#else
    (void)ws_server;
    (void)ws_fd;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
