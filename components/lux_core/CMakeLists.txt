idf_component_register(SRCS "src/dmx_buffer.c" "src/output_init.c" "src/dmx_tx_task.c" "src/sender_tracker.c" "src/merge_engine.c" "src/frame_router.c" "src/scene_stub.c"
                       INCLUDE_DIRS "include"
                       REQUIRES lux_common lux_config lux_drv log lux_log freertos esp_driver_rmt esp_timer)
