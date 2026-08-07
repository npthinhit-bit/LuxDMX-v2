#pragma once
#include <stdint.h>
#include "config_schema.h"
#include "config_enums.h"

#define MAX_SENDERS 16
#define SOURCE_TIMEOUT_MS 2500
#define DEFAULT_PRIORITY 100

struct Sender {
    uint32_t ip;
    uint8_t  proto;
    uint32_t lastMs;
    uint32_t winMs;
    uint16_t winCnt;
    float    fps;
    int16_t  universe;
    uint16_t dataLen;
    uint8_t  priority;
    uint8_t  data[512];
};

extern Sender senders[];

bool universeMapped(int universe);
void updateSender(uint32_t ip, uint8_t proto, int16_t universe,
                  uint8_t priority, const uint8_t* data, uint16_t length);
uint8_t activeSenderCount();
int sourcesOnUniverse(int universe, uint32_t windowMs);
bool hasConflict();
bool isMerging();
uint8_t sourceStatus();
