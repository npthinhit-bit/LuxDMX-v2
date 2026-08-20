idf_component_register(SRCS "src/dmx_rmt_tx.c"
                       INCLUDE_DIRS "include"
                       REQUIRES log lux_log freertos esp_driver_rmt)
