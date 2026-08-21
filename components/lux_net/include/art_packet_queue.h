#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ART_PKT_QUEUE_CAPACITY 32
#define ART_PKT_MAX_SIZE 640

typedef struct {
    uint16_t length;
    uint32_t sourceIp;
    uint8_t data[ART_PKT_MAX_SIZE];
} art_packet_t;

void artPktQueueInit(void);
bool artPktQueuePush(const art_packet_t* pkt);
bool artPktQueuePop(art_packet_t* pkt);
bool artPktQueueEmpty(void);

#ifdef __cplusplus
}
#endif
