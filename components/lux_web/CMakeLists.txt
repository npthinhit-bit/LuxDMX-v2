idf_component_register(SRCS "src/web_server.c" "src/web_routes.c" "src/web_frontend.c" "src/web_websocket.c" "src/web_assets.c" "src/ws_frame.c"
                     INCLUDE_DIRS "include" "../lux_core/include" "../lux_common/include" "../lux_net/include"
                     REQUIRES lux_common lux_config lux_core lux_net lux_log lux_wifi esp_http_server esp_netif esp_wifi nvs_flash)
