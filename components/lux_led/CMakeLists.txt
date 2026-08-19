idf_component_register(SRCS "src/led_driver.c"
                     INCLUDE_DIRS "include"
                     REQUIRES lux_common lux_hw lux_log esp_driver_gpio freertos)