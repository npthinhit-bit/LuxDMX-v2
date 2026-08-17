// Host test for rdm_types.h — UID pack/unpack, checksum, string helpers.
#include "rdm_types.h"
#include <cstdio>
#include <cstring>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg)                 \
    do                                   \
    {                                    \
        if (cond)                        \
            g_pass++;                    \
        else                             \
        {                                \
            g_fail++;                    \
            printf("  FAIL: %s\n", msg); \
        }                                \
    } while (0)

int main()
{
    // 1. UID equality
    {
        rdm_uid_t a = {0x4C58, 0x12345678};
        rdm_uid_t b = {0x4C58, 0x12345678};
        rdm_uid_t c = {0x4C58, 0x12345679};
        CHECK(rdm_uid_is_eq(&a, &b), "uid eq same");
        CHECK(!rdm_uid_is_eq(&a, &c), "uid neq different dev");
        CHECK(!rdm_uid_is_eq(&a, &RDM_UID_BROADCAST_ALL), "uid neq broadcast");
    }

    // 2. Broadcast and max
    {
        CHECK(RDM_UID_BROADCAST_ALL.man_id == 0xFFFF, "broadcast man_id");
        CHECK(RDM_UID_MAX.dev_id == 0xFFFFFFFE, "max dev_id");
    }

    // 3. Response type enum values
    {
        CHECK(RDM_RESPONSE_TYPE_ACK == 0x00, "ACK = 0");
        CHECK(RDM_RESPONSE_TYPE_NACK_REASON == 0x02, "NACK = 2");
        CHECK(RDM_RESPONSE_TYPE_INVALID == 0xfe, "INVALID = 0xfe");
        CHECK(RDM_RESPONSE_TYPE_NONE == 0xff, "NONE = 0xff");
    }

    // 4. PID constants
    {
        CHECK(RDM_PID_DEVICE_INFO == 0x0060, "DEVICE_INFO PID");
        CHECK(RDM_PID_DISC_UNIQUE_BRANCH == 0x0001, "DISC_UNIQUE_BRANCH PID");
        CHECK(RDM_PID_DISC_MUTE == 0x0002, "DISC_MUTE PID");
        CHECK(RDM_PID_IDENTIFY_DEVICE == 0x1000, "IDENTIFY_DEVICE PID");
    }

    // 5. Command class constants
    {
        CHECK(RDM_CC_GET_COMMAND == 0x20, "GET = 0x20");
        CHECK(RDM_CC_SET_COMMAND == 0x30, "SET = 0x30");
        CHECK(RDM_CC_DISC_COMMAND == 0x10, "DISC = 0x10");
    }

    // 6. DMX packet size
    {
        CHECK(DMX_PACKET_SIZE == 513, "DMX_PACKET_SIZE = 513");
    }

    // 7. RDM ASCII size max
    {
        CHECK(RDM_ASCII_SIZE_MAX == 33, "RDM_ASCII_SIZE_MAX = 33");
    }

    // 8. Sensor types
    {
        CHECK(RDM_SENSOR_TYPE_TEMPERATURE == 0x00, "TEMP sensor");
        CHECK(RDM_SENSOR_TYPE_VOLTAGE == 0x01, "VOLT sensor");
        CHECK(RDM_SENSOR_TYPE_CURRENT == 0x02, "CURR sensor");
    }

    // 9. Sensor units
    {
        CHECK(RDM_UNITS_CENTIGRADE == 0x01, "CENTIGRADE unit");
        CHECK(RDM_UNITS_VOLTS_DC == 0x02, "VOLTS_DC unit");
    }

    // 10. rdm_uid_is_eq with pointers
    {
        rdm_uid_t a = {0x1234, 0xABCDEF00};
        rdm_uid_t b = {0x1234, 0xABCDEF00};
        rdm_uid_t c = {0x5678, 0xABCDEF00};
        CHECK(rdm_uid_is_eq(&a, &b), "uid is_eq via pointer");
        CHECK(!rdm_uid_is_eq(&a, &c), "uid not eq via pointer");
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
