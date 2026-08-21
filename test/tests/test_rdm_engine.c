#include "rdm_engine.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_build_and_parse(void)
{
    const uint8_t params[] = {0x12, 0x34, 0x56};
    const rdm_request_t request = {
        .destination_uid = {.manufacturer_id = 0x1111, .device_id = 0x22223333},
        .source_uid = {.manufacturer_id = 0x4C58, .device_id = 0x01020304},
        .transaction_number = 7,
        .message_count = 0,
        .sub_device = 0,
        .command_class = RDM_CC_GET_COMMAND_RESPONSE,
        .pid = RDM_PID_DEVICE_INFO,
        .param_data = params,
        .param_data_length = sizeof(params),
    };
    uint8_t frame[RDM_REQUEST_MAX_SIZE] = {0};
    const size_t length = rdmBuild(frame, sizeof(frame), &request);
    assert(length == RDM_REQUEST_HEADER_SIZE + sizeof(params) + 2u);
    assert(frame[0] == RDM_SC && frame[1] == RDM_SUB_SC);
    assert(frame[2] == RDM_REQUEST_HEADER_SIZE + sizeof(params));
    assert(rdmChecksum(frame, frame[2]) == (uint16_t)((frame[frame[2]] << 8) | frame[frame[2] + 1u]));

    rdm_ack_t ack;
    const char *failure = NULL;
    assert(rdmReadResp(frame, length, &ack, &failure));
    assert(failure == NULL);
    assert(rdm_uid_equal(ack.source_uid, request.source_uid));
    assert(ack.pid == request.pid);
    assert(ack.pdl == sizeof(params));
    assert(memcmp(ack.param_data, params, sizeof(params)) == 0);
}

static void test_diagnostics(void)
{
    uint8_t frame[RDM_REQUEST_MAX_SIZE] = {0};
    rdm_ack_t ack;
    const char *failure = NULL;
    assert(!rdmReadResp(frame, RDM_RESPONSE_MIN_SIZE - 1u, &ack, &failure));
    assert(strcmp(failure, "short") == 0);

    memset(frame, 0x55, sizeof(frame));
    frame[0] = RDM_SC;
    frame[1] = RDM_SUB_SC;
    frame[2] = RDM_REQUEST_HEADER_SIZE;
    assert(!rdmReadResp(frame, RDM_RESPONSE_MIN_SIZE, &ack, &failure));
    assert(strcmp(failure, "ck") == 0);
}

int main(void)
{
    test_build_and_parse();
    test_diagnostics();
    puts("rdm_engine_test: 2 tests passed");
    return 0;
}
