// Unity unit tests for rdm_types.h -- UID, PID, response-type, sensor constants.
#include "rdm_types.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_uid_equality(void) {
    rdm_uid_t a = {0x4C58, 0x12345678};
    rdm_uid_t b = {0x4C58, 0x12345678};
    rdm_uid_t c = {0x4C58, 0x12345679};
    TEST_ASSERT_TRUE(rdm_uid_is_eq(&a, &b));
    TEST_ASSERT_FALSE(rdm_uid_is_eq(&a, &c));
    TEST_ASSERT_FALSE(rdm_uid_is_eq(&a, &RDM_UID_BROADCAST_ALL));
}

void test_uid_constants(void) {
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, RDM_UID_BROADCAST_ALL.man_id);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFE, RDM_UID_MAX.dev_id);
}

void test_pid_constants(void) {
    TEST_ASSERT_EQUAL_HEX16(0x0001, RDM_PID_DISC_UNIQUE_BRANCH);
    TEST_ASSERT_EQUAL_HEX16(0x0002, RDM_PID_DISC_MUTE);
    TEST_ASSERT_EQUAL_HEX16(0x0030, RDM_PID_STATUS_MESSAGE);
    TEST_ASSERT_EQUAL_HEX16(0x0060, RDM_PID_DEVICE_INFO);
    TEST_ASSERT_EQUAL_HEX16(0x1000, RDM_PID_IDENTIFY_DEVICE);
    TEST_ASSERT_EQUAL_HEX16(0x1010, RDM_PID_DEVICE_HOURS);
    TEST_ASSERT_EQUAL_HEX16(0x1011, RDM_PID_IDENTIFY_MODE);
    TEST_ASSERT_EQUAL_HEX16(0x1012, RDM_PID_DEVICE_POWER);
    TEST_ASSERT_EQUAL_HEX16(0x1013, RDM_PID_BURN_IN);
    TEST_ASSERT_EQUAL_HEX16(0x0200, RDM_PID_SENSOR_DEFINITION);
    TEST_ASSERT_EQUAL_HEX16(0x0201, RDM_PID_SENSOR_VALUE);
}

void test_response_type_enum(void) {
    TEST_ASSERT_EQUAL(0, RDM_RESPONSE_TYPE_ACK);
    TEST_ASSERT_EQUAL(2, RDM_RESPONSE_TYPE_NACK_REASON);
    TEST_ASSERT_EQUAL(0xfe, RDM_RESPONSE_TYPE_INVALID);
    TEST_ASSERT_EQUAL(0xff, RDM_RESPONSE_TYPE_NONE);
}

void test_command_class_enum(void) {
    TEST_ASSERT_EQUAL(0x10, RDM_CC_DISC_COMMAND);
    TEST_ASSERT_EQUAL(0x20, RDM_CC_GET_COMMAND);
    TEST_ASSERT_EQUAL(0x30, RDM_CC_SET_COMMAND);
}

void test_sensor_type_enum(void) {
    TEST_ASSERT_EQUAL(0, RDM_SENSOR_TYPE_TEMPERATURE);
    TEST_ASSERT_EQUAL(1, RDM_SENSOR_TYPE_VOLTAGE);
    TEST_ASSERT_EQUAL(2, RDM_SENSOR_TYPE_CURRENT);
    TEST_ASSERT_EQUAL(3, RDM_SENSOR_TYPE_FREQUENCY);
    TEST_ASSERT_EQUAL(5, RDM_SENSOR_TYPE_POWER);
    TEST_ASSERT_EQUAL(0x1f, RDM_SENSOR_TYPE_HUMIDITY);
}

void test_dmx_packet_size(void) {
    TEST_ASSERT_EQUAL(513, DMX_PACKET_SIZE);
    TEST_ASSERT_EQUAL(33, RDM_ASCII_SIZE_MAX);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_uid_equality);
    RUN_TEST(test_uid_constants);
    RUN_TEST(test_pid_constants);
    RUN_TEST(test_response_type_enum);
    RUN_TEST(test_command_class_enum);
    RUN_TEST(test_sensor_type_enum);
    RUN_TEST(test_dmx_packet_size);
    return UNITY_END();
}
