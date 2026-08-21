idf_component_register(SRCS "src/art_packet_queue.c" "src/artnet.c" "src/artnet_bridge.c" "src/net_rx_task.c" "src/sacn_pkt_queue.c" "src/sacn.c" "src/ota_sign.c" "src/ota_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES lux_common lux_config lux_core lux_log freertos mbedtls app_update spi_flash esp_http_server)