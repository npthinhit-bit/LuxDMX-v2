idf_component_register(SRCS "src/led_driver.c" "src/led_simple.c" "src/led_ws2812.c" "src/led_panel.c" "src/led_task.c"
                    INCLUDE_DIRS "include"
                    REQUIRES lux_common lux_hw driver rmt)
