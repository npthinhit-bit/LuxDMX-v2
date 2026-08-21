/*
 * Native test shim - WebSocket
 */
#pragma once

#include "httpd.h"

esp_err_t web_websocket_init(httpd_handle_t server);
esp_err_t web_websocket_send_status(void);
