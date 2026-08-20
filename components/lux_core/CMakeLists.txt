idf_component_register(SRCS "src/dmx_buffer.c" "src/output_init.c" "src/dmx_tx_task.c"
                       INCLUDE_DIRS "include"
                       REQUIRES lux_common lux_config lux_drv log lux_log freertos esp_driver_rmt)
