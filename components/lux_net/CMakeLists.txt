idf_component_register(SRCS "src/art_packet_queue.c" "src/artnet.c" "src/net_rx_task.c"
                       INCLUDE_DIRS "include"
                       REQUIRES lux_common lux_config lux_core lux_log freertos)
