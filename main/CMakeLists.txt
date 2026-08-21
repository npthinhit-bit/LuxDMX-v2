idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_rmt lux_common lux_hw lux_wifi lux_net lux_led lux_config lux_web lux_core lux_drv lux_log lux_sys cjson nvs_flash esp_wifi esp_netif esp_http_server)
