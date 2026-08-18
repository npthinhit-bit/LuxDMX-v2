idf_component_register(SRCS "src/wifi_manager.c" "src/wifi_events.c" "src/captive_portal.c" "src/wifi_config.c"
                    INCLUDE_DIRS "include"
                    REQUIRES lux_common esp_wifi esp_netif esp_event)
