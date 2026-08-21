#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RDM_SC 0xCCu
#define RDM_SUB_SC 0x01u
#define RDM_UID_BYTES 6u
#define RDM_UID_MIN 0x000000000000ULL
#define RDM_UID_MAX 0xFFFFFFFFFFFFULL
#define RDM_ASCII_SIZE_MAX 32u
#define RDM_MAX_PARAM_DATA 231u
#define RDM_MAX_LINES 2u
#define RDM_MAX_RETRIES 3u
#define RDM_RESP_TIMEOUT_MS 9u
#define RDM_DISC_TIMEOUT_MS 6u

#define RDM_MANUFACTURER_LUXDMX 0x4C58u
#define RDM_UID_BROADCAST_ALL ((uint64_t)RDM_UID_MAX)

#define RDM_PID_DISC_UNIQUE_BRANCH 0x0001u
#define RDM_PID_DISC_MUTE 0x0002u
#define RDM_PID_DISC_UN_MUTE 0x0003u
#define RDM_PID_SUPPORTED_PARAMETERS 0x0050u
#define RDM_PID_DEVICE_INFO 0x0060u
#define RDM_PID_SOFTWARE_VERSION_LABEL 0x00C0u
#define RDM_PID_IDENTIFY_DEVICE 0x1000u
#define RDM_PID_SET_IDENTIFY 0x1001u
#define RDM_PID_STATUS_MESSAGES 0x0030u
#define RDM_PID_SUB_DEVICE_STATUS_REPORT_THRESHOLD 0x0032u
#define RDM_PID_DMX_START_ADDRESS 0x00F0u
#define RDM_PID_SLOT_INFO 0x0120u
#define RDM_PID_SLOT_DESCRIPTION 0x0121u
#define RDM_PID_MANUFACTURER_LABEL 0x0081u

/* RDM command classes. */
typedef enum {
    RDM_CC_DISCOVERY_COMMAND = 0x10,
    RDM_CC_GET_COMMAND = 0x20,
    RDM_CC_SET_COMMAND = 0x30,
    RDM_CC_DISCOVERY_COMMAND_RESPONSE = 0x11,
    RDM_CC_GET_COMMAND_RESPONSE = 0x21,
    RDM_CC_SET_COMMAND_RESPONSE = 0x31,
} rdm_command_class_t;

/* RDM response types carried in the response message header. */
typedef enum {
    RDM_RESPONSE_NONE = 0,
    RDM_RESPONSE_ACK = 1,
    RDM_RESPONSE_ACK_TIMER = 2,
    RDM_RESPONSE_NACK_REASON = 3,
    RDM_RESPONSE_INVALID = 4,
} rdm_response_type_t;

typedef enum {
    RDM_SENSOR_UNIT_NONE = 0,
    RDM_SENSOR_UNIT_CELSIUS = 1,
    RDM_SENSOR_UNIT_VOLTS = 2,
    RDM_SENSOR_UNIT_AMPS = 3,
    RDM_SENSOR_UNIT_HERTZ = 4,
    RDM_SENSOR_UNIT_SECONDS = 5,
    RDM_SENSOR_UNIT_PERCENT = 6,
} rdm_sensor_unit_t;

typedef struct __attribute__((packed)) {
    uint16_t manufacturer_id;
    uint32_t device_id;
} rdm_uid_t;

typedef uint16_t rdm_pid_t;

typedef struct __attribute__((packed)) {
    rdm_uid_t source_uid;
    rdm_response_type_t response_type;
    uint8_t message_count;
    rdm_pid_t pid;
    uint8_t pdl;
    uint8_t param_data[RDM_MAX_PARAM_DATA];
} rdm_ack_t;

static inline uint64_t rdm_uid_pack(rdm_uid_t uid)
{
    return ((uint64_t)uid.manufacturer_id << 32) | (uint64_t)uid.device_id;
}

static inline rdm_uid_t rdm_uid_unpack(uint64_t packed)
{
    rdm_uid_t uid;
    uid.manufacturer_id = (uint16_t)((packed >> 32) & 0xFFFFu);
    uid.device_id = (uint32_t)(packed & 0xFFFFFFFFu);
    return uid;
}

static inline bool rdm_uid_equal(rdm_uid_t a, rdm_uid_t b)
{
    return a.manufacturer_id == b.manufacturer_id && a.device_id == b.device_id;
}

static inline void rdm_uid_to_wire(rdm_uid_t uid, uint8_t out[RDM_UID_BYTES])
{
    out[0] = (uint8_t)(uid.manufacturer_id >> 8);
    out[1] = (uint8_t)uid.manufacturer_id;
    out[2] = (uint8_t)(uid.device_id >> 24);
    out[3] = (uint8_t)(uid.device_id >> 16);
    out[4] = (uint8_t)(uid.device_id >> 8);
    out[5] = (uint8_t)uid.device_id;
}

static inline rdm_uid_t rdm_uid_from_wire(const uint8_t in[RDM_UID_BYTES])
{
    rdm_uid_t uid;
    uid.manufacturer_id = (uint16_t)(((uint16_t)in[0] << 8) | in[1]);
    uid.device_id = ((uint32_t)in[2] << 24) | ((uint32_t)in[3] << 16) |
                    ((uint32_t)in[4] << 8) | in[5];
    return uid;
}

#ifdef __cplusplus
}
#endif
