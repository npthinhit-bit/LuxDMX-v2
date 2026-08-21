#include "rdm_engine.h"

#include <string.h>

static void write_uid(uint8_t *dst, rdm_uid_t uid)
{
    rdm_uid_to_wire(uid, dst);
}

static uint16_t read_be16(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8) | src[1]);
}

uint16_t rdmChecksum(const uint8_t *packet, size_t message_length)
{
    uint16_t checksum = 0;
    if (packet == NULL) {
        return 0;
    }
    for (size_t i = 0; i < message_length; ++i) {
        checksum = (uint16_t)(checksum + packet[i]);
    }
    return checksum;
}

size_t rdmBuild(uint8_t *out, size_t capacity, const rdm_request_t *request)
{
    if (out == NULL || request == NULL || request->param_data_length > RDM_MAX_PARAM_DATA) {
        return 0;
    }

    const size_t message_length = RDM_REQUEST_HEADER_SIZE + request->param_data_length;
    const size_t total_length = message_length + 2u;
    if (capacity < total_length) {
        return 0;
    }

    memset(out, 0, total_length);
    out[0] = RDM_SC;
    out[1] = RDM_SUB_SC;
    out[2] = (uint8_t)message_length;
    write_uid(&out[3], request->destination_uid);
    write_uid(&out[9], request->source_uid);
    out[15] = request->transaction_number;
    out[16] = request->message_count;
    out[17] = 0;
    out[18] = (uint8_t)(request->sub_device >> 8);
    out[19] = (uint8_t)request->sub_device;
    out[20] = (uint8_t)request->command_class;
    out[21] = (uint8_t)(request->pid >> 8);
    out[22] = (uint8_t)request->pid;
    out[23] = request->param_data_length;
    if (request->param_data_length != 0 && request->param_data != NULL) {
        memcpy(&out[24], request->param_data, request->param_data_length);
    }

    const uint16_t checksum = rdmChecksum(out, message_length);
    out[message_length] = (uint8_t)(checksum >> 8);
    out[message_length + 1u] = (uint8_t)checksum;
    return total_length;
}

static rdm_response_type_t response_type_from_header(uint8_t command_class)
{
    switch (command_class & 0x0Fu) {
    case 0x01: return RDM_RESPONSE_ACK;
    case 0x02: return RDM_RESPONSE_ACK_TIMER;
    case 0x03: return RDM_RESPONSE_NACK_REASON;
    default: return command_class == RDM_CC_GET_COMMAND_RESPONSE ||
                    command_class == RDM_CC_SET_COMMAND_RESPONSE
                ? RDM_RESPONSE_ACK : RDM_RESPONSE_INVALID;
    }
}

bool rdmReadResp(const uint8_t *packet, size_t length, rdm_ack_t *ack,
                 const char **failure_label)
{
    static const char *const short_label = "short";
    static const char *const no_sc_label = "noSC";
    static const char *const length_label = "len";
    static const char *const checksum_label = "ck";

    if (failure_label != NULL) {
        *failure_label = NULL;
    }
    if (packet == NULL || ack == NULL || length < RDM_RESPONSE_MIN_SIZE) {
        if (failure_label != NULL) *failure_label = short_label;
        return false;
    }

    size_t start = 0;
    while (start + 1u < length &&
           (packet[start] != RDM_SC || packet[start + 1u] != RDM_SUB_SC)) {
        ++start;
    }
    if (start + 1u >= length) {
        if (failure_label != NULL) *failure_label = no_sc_label;
        return false;
    }

    const size_t message_length = packet[start + 2u];
    if (message_length < RDM_REQUEST_HEADER_SIZE ||
        start + message_length + 1u >= length) {
        if (failure_label != NULL) *failure_label = length_label;
        return false;
    }

    const uint16_t expected = read_be16(&packet[start + message_length]);
    const uint16_t actual = rdmChecksum(&packet[start], message_length);
    if (expected != actual) {
        if (failure_label != NULL) *failure_label = checksum_label;
        return false;
    }

    memset(ack, 0, sizeof(*ack));
    ack->source_uid = rdm_uid_from_wire(&packet[start + 9u]);
    ack->response_type = response_type_from_header(packet[start + 20u]);
    ack->message_count = packet[start + 17u];
    ack->pid = read_be16(&packet[start + 21u]);
    ack->pdl = packet[start + 23u];
    if (ack->pdl > RDM_MAX_PARAM_DATA || 24u + ack->pdl > message_length) {
        if (failure_label != NULL) *failure_label = length_label;
        return false;
    }
    if (ack->pdl != 0) {
        memcpy(ack->param_data, &packet[start + 24u], ack->pdl);
    }
    return true;
}
