idf_component_register(SRCS "src/crash_guard.c" "src/syslog_client.c"
                       INCLUDE_DIRS "include"
                       REQUIRES lux_common lux_config lux_core lux_log freertos nvs_flash lwip)
