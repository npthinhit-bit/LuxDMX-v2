#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SACN_PORT 5568
#define SACN_MIN_SIZE 638
#define SACN_PKT_QUEUE_CAP 16
#define SACN_PER_SOCKET_LIMIT 4
#define SACN_STREAM_VEC 0x04

#define SACN_ROOT_VEC_OFFSET 6
#define SACN_FRAME_VEC_OFFSET 28
#define SACN_PRIORITY_OFFSET 48
#define SACN_UNIVERSE_OFFSET 52
#define SACN_START_CODE_OFFSET 57
#define SACN_DMX_DATA_OFFSET 58

typedef struct {
    uint16_t len;
    uint16_t outputIdx;
    uint32_t sourceIp;
    uint8_t data[SACN_MIN_SIZE];
} SAcnPacket;

esp_err_t sacn_init(void);
void sacn_poll(void);
void sacn_check_timeouts(void);
bool sacn_dispatch_packet(const uint8_t* data, uint16_t len, uint32_t sourceIp);

void sacn_pkt_queue_init(void);
bool sacn_pkt_queue_push(const SAcnPacket* pkt);
bool sacn_pkt_queue_pop(SAcnPacket* pkt);

#ifdef __cplusplus
}
#endif
