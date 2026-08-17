#pragma once
#include "config_enums.h"
#include "config_schema.h"
#include <stdint.h>

#ifndef CONFIG_LUXDMX_MAX_SENDERS
#define CONFIG_LUXDMX_MAX_SENDERS 16
#endif
#define MAX_SENDERS CONFIG_LUXDMX_MAX_SENDERS
#define SOURCE_TIMEOUT_MS 2500
#define DEFAULT_PRIORITY 100

struct Sender
{
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

struct SenderTracker
{
    Sender senders[MAX_SENDERS];
};
SenderTracker& senderTracker();

bool universeMapped(int universe);
void updateSender(uint32_t ip, uint8_t proto, int16_t universe, uint8_t priority, const uint8_t* data, uint16_t length);
uint8_t activeSenderCount();
int     sourcesOnUniverse(int universe, uint32_t windowMs);
bool    hasConflict();
bool    isMerging();
uint8_t sourceStatus();
