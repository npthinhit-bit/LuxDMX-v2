idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES lux_common lux_hw lux_wifi lux_led lux_config lux_web lux_log cjson nvs_flash esp_wifi esp_netif esp_http_server)
