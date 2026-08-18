idf_component_register(SRCS "src/web_server.c" "src/web_routes.c" "src/web_frontend.c" "src/web_websocket.c" "src/web_assets.c"
                    INCLUDE_DIRS "include"
                    REQUIRES lux_common lux_config esp_http_server esp_websocket esp_tls)
