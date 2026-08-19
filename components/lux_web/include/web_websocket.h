#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// WebSocket interface
esp_err_t web_websocket_init(httpd_handle_t server);
esp_err_t web_websocket_send_status(void);

#ifdef __cplusplus
}
#endif
