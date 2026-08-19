idf_component_register(SRCS "src/utils.c" "src/boards.c"
                     INCLUDE_DIRS "include"
                     REQUIRES log lux_log freertos)