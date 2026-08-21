#pragma once

#include "rdm_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RDM_REQUEST_HEADER_SIZE 24u
#define RDM_REQUEST_MAX_SIZE (RDM_REQUEST_HEADER_SIZE + RDM_MAX_PARAM_DATA + 2u)
#define RDM_RESPONSE_MIN_SIZE 26u

typedef struct {
    rdm_uid_t destination_uid;
    rdm_uid_t source_uid;
    uint8_t transaction_number;
    uint8_t message_count;
    uint16_t sub_device;
    rdm_command_class_t command_class;
    rdm_pid_t pid;
    const uint8_t *param_data;
    uint8_t param_data_length;
} rdm_request_t;

/* Build one E1.20 packet. Returns total wire bytes including checksum. */
size_t rdmBuild(uint8_t *out, size_t capacity, const rdm_request_t *request);

/* Parse a response and expose a stable diagnostic label on failure. */
bool rdmReadResp(const uint8_t *packet, size_t length, rdm_ack_t *ack,
                 const char **failure_label);

uint16_t rdmChecksum(const uint8_t *packet, size_t message_length);

#ifdef __cplusplus
}
#endif
