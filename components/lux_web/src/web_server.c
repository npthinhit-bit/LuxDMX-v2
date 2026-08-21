#include "web_server.h"
#include "common.h"
#include "logger.h"
#include "web_routes.h"
#include "web_websocket.h"
#include "ota_manager.h"
#include "config_engine.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "web_server";
static httpd_handle_t server = NULL;

// Custom URI match function for WebSocket
static bool ws_uri_match(const char* uri_template, const char* uri_to_match, size_t match_upto) {
    return strncmp(uri_template, uri_to_match, match_upto) == 0;
}

esp_err_t web_server_init(void) {
    LOG_INFO(TAG, "Initializing web server");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20;
    config.max_open_sockets = 7;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;
    config.uri_match_fn = ws_uri_match;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    otaManagerInit();

    // Register routes
    err = web_routes_register(server);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to register routes: %s", esp_err_to_name(err));
        httpd_stop(server);
        server = NULL;
        return err;
    }

    err = otaManagerRegister(server);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to initialize OTA routes: %s", esp_err_to_name(err));
        httpd_stop(server);
        server = NULL;
        return err;
    }

    // Initialize WebSocket
    err = web_websocket_init(server);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "Failed to initialize WebSocket: %s", esp_err_to_name(err));
        httpd_stop(server);
        server = NULL;
        return err;
    }

    LOG_INFO(TAG, "Web server initialized");
    return ESP_OK;
}

esp_err_t web_server_start(void) {
    if (server == NULL) {
        return web_server_init();
    }
    return ESP_OK;
}

esp_err_t web_server_stop(void) {
    if (server != NULL) {
        esp_err_t err = httpd_stop(server);
        server = NULL;
        LOG_INFO(TAG, "Web server stopped");
        return err;
    }
    return ESP_OK;
}

httpd_handle_t web_server_get_handle(void) {
    return server;
}