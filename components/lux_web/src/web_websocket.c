#include "web_websocket.h"
#include "logger.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "web_websocket";
static httpd_handle_t ws_server = NULL;
static int ws_fd = -1;

// WebSocket handler
static esp_err_t ws_handler(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        // WebSocket handshake
        ESP_LOGI(TAG, "WebSocket handshake requested");
        httpd_resp_set_type(req, "application/octet-stream");
        httpd_resp_set_hdr(req, "Upgrade", "websocket");
        httpd_resp_set_hdr(req, "Connection", "Upgrade");
        httpd_resp_set_status(req, "101 Switching Protocols");
        return httpd_resp_send(req, NULL, 0);
    }

    return ESP_OK;
}

esp_err_t web_websocket_init(httpd_handle_t server) {
    ws_server = server;

    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL
    };

    esp_err_t err = httpd_register_uri_handler(server, &ws_uri);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to register WebSocket handler: %s", esp_err_to_name(err));
        return err;
    }

    LOG_INFO(TAG, "WebSocket initialized");
    return ESP_OK;
}

esp_err_t web_websocket_send_status(void) {
    // TODO: Implement WebSocket status broadcast
    // For now, just return OK
    return ESP_OK;
}