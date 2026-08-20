#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ARTNET_PORT 6454
#define ARTNET_ID "Art-Net\0"
#define ARTNET_MIN_PACKET 12
#define ARTNET_MAX_PACKET 640
#define MAX_PACKETS_PER_TICK 8

typedef struct {
    int sock;
    bool ready;
    bool rdmEnabled;
    uint32_t localIp;
} ArtNetState;

extern ArtNetState g_artnet;

esp_err_t artnet_init(void);
void artnet_poll(void);
bool artnet_dispatch_packet(const uint8_t* data, uint16_t len, uint32_t sourceIp);
void net_rx_task_start(void);

#ifdef __cplusplus
}
#endif