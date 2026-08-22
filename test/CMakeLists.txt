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
    ${CMAKE_SOURCE_DIR}/../components/lux_core/include
    ${CMAKE_SOURCE_DIR}/../components/lux_net/include
    ${CMAKE_SOURCE_DIR}/fixtures
)

# Include cJSON directly (not as a component, just the header)
include_directories(${CMAKE_SOURCE_DIR}/../components/cjson)

# cJSON as a static library (raw .c path in target_link_libraries works with
# gcc linker driver but not MSVC link.exe, which expects object/libs)
add_library(cjson_test STATIC ${CMAKE_SOURCE_DIR}/../components/cjson/cJSON.c)

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
    ${CMAKE_SOURCE_DIR}/../components/lux_core/src/dmx_buffer.c
    ${CMAKE_SOURCE_DIR}/../components/lux_core/src/sender_tracker.c
    ${CMAKE_SOURCE_DIR}/../components/lux_core/src/merge_engine.c
    ${CMAKE_SOURCE_DIR}/../components/lux_core/src/rdm_engine.c
    ${CMAKE_SOURCE_DIR}/../components/lux_core/src/rdm_discovery.c
)

add_library(lux_wifi_test STATIC
    ${CMAKE_SOURCE_DIR}/../components/lux_wifi/src/wifi_manager.c
    ${CMAKE_SOURCE_DIR}/../components/lux_wifi/src/wifi_config.c
)
target_compile_definitions(lux_wifi_test PUBLIC UNIT_TEST)

# Test executables
add_executable(logger_test ${CMAKE_SOURCE_DIR}/tests/test_logger.c)
target_link_libraries(logger_test lux_log_test lux_common_test shim cjson_test)

add_executable(config_serial_test ${CMAKE_SOURCE_DIR}/tests/test_config_serial.c)
target_link_libraries(config_serial_test lux_config_test lux_log_test lux_common_test lux_wifi_test shim cjson_test)

add_executable(seqlock_test ${CMAKE_SOURCE_DIR}/tests/test_seqlock.c)
target_link_libraries(seqlock_test lux_config_test shim cjson_test)

add_executable(merge_test ${CMAKE_SOURCE_DIR}/tests/test_merge.c)
target_link_libraries(merge_test lux_config_test shim cjson_test)

add_executable(config_migration_test ${CMAKE_SOURCE_DIR}/tests/test_config_migration.c)
target_link_libraries(config_migration_test lux_config_test lux_log_test lux_common_test lux_wifi_test shim cjson_test)

add_executable(portal_test ${CMAKE_SOURCE_DIR}/tests/test_portal.c)
target_link_libraries(portal_test lux_wifi_test lux_config_test lux_log_test lux_common_test shim cjson_test)

add_executable(boards_test ${CMAKE_SOURCE_DIR}/tests/test_boards.c)
target_link_libraries(boards_test lux_common_test lux_log_test shim cjson_test)

add_executable(net_baseline_test ${CMAKE_SOURCE_DIR}/tests/test_net_baseline.c)
target_link_libraries(net_baseline_test lux_wifi_test lux_config_test lux_log_test lux_common_test shim cjson_test)

add_executable(led_math_test ${CMAKE_SOURCE_DIR}/tests/test_led_math.c)
target_link_libraries(led_math_test shim)

add_executable(fixture_contract_test ${CMAKE_SOURCE_DIR}/tests/test_fixture_contract.c)

add_library(fixture_report STATIC
    ${CMAKE_SOURCE_DIR}/fixtures/test_fixture_report.c
)

add_executable(fixture_report_test ${CMAKE_SOURCE_DIR}/tests/test_fixture_report.c)
target_link_libraries(fixture_report_test fixture_report)

add_library(artnet_fixture STATIC
    ${CMAKE_SOURCE_DIR}/fixtures/artnet_fixture.c
)

add_executable(artnet_fixture_test ${CMAKE_SOURCE_DIR}/tests/test_artnet_fixture.c)
target_link_libraries(artnet_fixture_test artnet_fixture)

add_library(artnet_dispatch_test STATIC
    ${CMAKE_SOURCE_DIR}/../components/lux_net/src/artnet.c
)
target_compile_definitions(artnet_dispatch_test PRIVATE UNIT_TEST)

add_executable(artnet_dispatch_test_runner ${CMAKE_SOURCE_DIR}/tests/test_artnet_dispatch.c)
target_link_libraries(artnet_dispatch_test_runner artnet_dispatch_test artnet_fixture lux_log_test shim)

add_library(ota_recovery_policy_lib STATIC
    ${CMAKE_SOURCE_DIR}/../components/lux_net/src/ota_recovery_policy.c
)
target_include_directories(ota_recovery_policy_lib PUBLIC ${CMAKE_SOURCE_DIR}/../components/lux_net/include)

add_executable(ota_recovery_policy_test ${CMAKE_SOURCE_DIR}/tests/test_ota_recovery_policy.c)
target_link_libraries(ota_recovery_policy_test ota_recovery_policy_lib)

add_library(art_packet_queue_lib STATIC
    ${CMAKE_SOURCE_DIR}/../components/lux_net/src/art_packet_queue.c
)
target_include_directories(art_packet_queue_lib PUBLIC ${CMAKE_SOURCE_DIR}/../components/lux_net/include)

add_executable(art_packet_queue_test ${CMAKE_SOURCE_DIR}/tests/test_art_packet_queue.c)
target_link_libraries(art_packet_queue_test art_packet_queue_lib shim cjson_test)

add_executable(rdm_types_test ${CMAKE_SOURCE_DIR}/tests/test_rdm_types.c)
target_link_libraries(rdm_types_test lux_common_test)

add_executable(rdm_engine_test ${CMAKE_SOURCE_DIR}/tests/test_rdm_engine.c)
target_link_libraries(rdm_engine_test lux_config_test lux_common_test shim)

add_library(ws_frame_test STATIC
    ${CMAKE_SOURCE_DIR}/../components/lux_web/src/ws_frame.c
)
target_include_directories(ws_frame_test PUBLIC ${CMAKE_SOURCE_DIR}/../components/lux_web/include)

add_executable(ws_frame_test_runner ${CMAKE_SOURCE_DIR}/tests/test_ws_frame.c)
target_link_libraries(ws_frame_test_runner ws_frame_test)

add_executable(rdm_discovery_test ${CMAKE_SOURCE_DIR}/tests/test_rdm_discovery.c)
target_link_libraries(rdm_discovery_test lux_config_test lux_common_test)

enable_testing()

add_test(logger_test logger_test)
add_test(config_serial_test config_serial_test)
add_test(seqlock_test seqlock_test)
add_test(merge_test merge_test)
add_test(config_migration_test config_migration_test)
add_test(portal_test portal_test)
add_test(boards_test boards_test)
add_test(net_baseline_test net_baseline_test)
add_test(led_math_test led_math_test)
add_test(fixture_contract_test fixture_contract_test)
add_test(fixture_report_test fixture_report_test)
add_test(artnet_fixture_test artnet_fixture_test)
add_test(artnet_dispatch_test artnet_dispatch_test_runner)
add_test(ota_recovery_policy_test ota_recovery_policy_test)
add_test(art_packet_queue_test art_packet_queue_test)
add_test(rdm_types_test rdm_types_test)
add_test(rdm_engine_test rdm_engine_test)
add_test(ws_frame_test ws_frame_test_runner)
add_test(rdm_discovery_test rdm_discovery_test)

# Native tests must fail deterministically instead of hanging a CI worker.
set_tests_properties(
    logger_test config_serial_test seqlock_test merge_test
    config_migration_test portal_test boards_test net_baseline_test
    led_math_test fixture_contract_test fixture_report_test artnet_fixture_test artnet_dispatch_test ota_recovery_policy_test art_packet_queue_test
    rdm_types_test rdm_engine_test ws_frame_test rdm_discovery_test
    PROPERTIES
        TIMEOUT 10
        LABELS native
)
