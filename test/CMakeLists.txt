# Native test build for LuxDMX-v2 Phase 1
# Uses PlatformIO native environment with ESP-IDF shims

cmake_minimum_required(VERSION 3.16)
project(luxdmx_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Include shim headers first (these provide ESP-IDF stubs)
include_directories(
    ${CMAKE_SOURCE_DIR}/shim
    ${CMAKE_SOURCE_DIR}/../components/lux_common/include
    ${CMAKE_SOURCE_DIR}/../components/lux_log/include
    ${CMAKE_SOURCE_DIR}/../components/lux_hw/include
    ${CMAKE_SOURCE_DIR}/../components/lux_config/include
    ${CMAKE_SOURCE_DIR}/../components/lux_wifi/include
    ${CMAKE_SOURCE_DIR}/../components/lux_led/include
)

# Include cJSON directly (not as a component, just the header)
include_directories(${CMAKE_SOURCE_DIR}/../components/cjson)

# Shim implementation
add_library(shim STATIC
    ${CMAKE_SOURCE_DIR}/shim/shim.c
)
target_include_directories(shim PUBLIC ${CMAKE_SOURCE_DIR}/shim)

# Component sources for testing (excluding hardware-specific init)
add_library(lux_common_test STATIC
    ${CMAKE_SOURCE_DIR}/../components/lux_common/src/boards.c
    ${CMAKE_SOURCE_DIR}/../components/lux_common/src/utils.c
)

add_library(lux_log_test STATIC
    ${CMAKE_SOURCE_DIR}/../components/lux_log/src/logger.c
)

add_library(lux_config_test STATIC
    ${CMAKE_SOURCE_DIR}/../components/lux_config/src/config_schema.c
    ${CMAKE_SOURCE_DIR}/../components/lux_config/src/config_engine.c
    ${CMAKE_SOURCE_DIR}/../components/lux_config/src/config_serial.c
    ${CMAKE_SOURCE_DIR}/../components/lux_config/src/config_json.c
)

add_library(lux_wifi_test STATIC
    ${CMAKE_SOURCE_DIR}/../components/lux_wifi/src/wifi_manager.c
    ${CMAKE_SOURCE_DIR}/../components/lux_wifi/src/wifi_config.c
)
target_compile_definitions(lux_wifi_test PUBLIC UNIT_TEST)

# Test executables
add_executable(logger_test ${CMAKE_SOURCE_DIR}/tests/test_logger.c)
target_link_libraries(logger_test lux_log_test lux_common_test shim ${CMAKE_SOURCE_DIR}/../components/cjson/cJSON.c)

add_executable(config_serial_test ${CMAKE_SOURCE_DIR}/tests/test_config_serial.c)
target_link_libraries(config_serial_test lux_config_test lux_log_test lux_common_test lux_wifi_test shim ${CMAKE_SOURCE_DIR}/../components/cjson/cJSON.c)

add_executable(portal_test ${CMAKE_SOURCE_DIR}/tests/test_portal.c)
target_link_libraries(portal_test lux_wifi_test lux_config_test lux_log_test lux_common_test shim ${CMAKE_SOURCE_DIR}/../components/cjson/cJSON.c)

add_executable(boards_test ${CMAKE_SOURCE_DIR}/tests/test_boards.c)
target_link_libraries(boards_test lux_common_test lux_log_test shim ${CMAKE_SOURCE_DIR}/../components/cjson/cJSON.c)

add_executable(net_baseline_test ${CMAKE_SOURCE_DIR}/tests/test_net_baseline.c)
target_link_libraries(net_baseline_test lux_wifi_test lux_config_test lux_log_test lux_common_test shim ${CMAKE_SOURCE_DIR}/../components/cjson/cJSON.c)

add_executable(led_math_test ${CMAKE_SOURCE_DIR}/tests/test_led_math.c)
target_link_libraries(led_math_test shim)

enable_testing()

add_test(logger_test logger_test)
add_test(config_serial_test config_serial_test)
add_test(portal_test portal_test)
add_test(boards_test boards_test)
add_test(net_baseline_test net_baseline_test)
add_test(led_math_test led_math_test)
