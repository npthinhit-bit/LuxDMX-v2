idf_component_register(SRCS "src/hw.c"
                     INCLUDE_DIRS "include"
                     REQUIRES lux_common lux_log esp_driver_gpio esp_hw_support esp_psram freertos heap)