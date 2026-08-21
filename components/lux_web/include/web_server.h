#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

// Web server interface
esp_err_t web_server_init(void);
esp_err_t web_server_start(void);
esp_err_t web_server_stop(void);

// WebSocket interface
esp_err_t web_websocket_init(httpd_handle_t server);
esp_err_t web_websocket_send_status(void);

#ifdef __cplusplus
}
#endif
