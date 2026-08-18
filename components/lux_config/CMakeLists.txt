idf_component_register(SRCS "src/config_schema.c" "src/config_engine.c" "src/config_templates.c" "src/config_serial.c" "src/config_json.c"
                    INCLUDE_DIRS "include"
                    REQUIRES lux_common nvs_flash cJSON)
