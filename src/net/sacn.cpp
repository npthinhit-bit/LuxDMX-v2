#include "sacn.h"
#include "config_schema.h"
#include "merge_engine.h"   // viewOutput
#include "frame_router.h"   // routeFrame
#include <Arduino.h>

const uint8_t ACN_PACKET_ID[12] = {
    0x41, 0x53, 0x43, 0x2d, 0x45, 0x31, 0x2e, 0x31,
    0x37, 0x00, 0x00, 0x00
};

WiFiUDP sacnUdp[MAX_OUTPUTS];
uint8_t sacnBuf[638];

void startSacn() {
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        sacnUdp[i].stop();
        if (!cfg.outputs[i].enabled) continue;
        uint16_t sacnUniverse = (uint16_t)(cfg.outputs[i].universe + 1);
        uint8_t  univHigh     = (uint8_t)((sacnUniverse >> 8) & 0xFF);
        uint8_t  univLow      = (uint8_t)(sacnUniverse & 0xFF);
        IPAddress mcast(239, 255, univHigh, univLow);
        sacnUdp[i].beginMulticast(mcast, 5568);
        Serial.printf("[sACN] out%d universe %u  multicast 239.255.%u.%u:5568\n",
                      i, sacnUniverse, univHigh, univLow);
    }
}

void readSacnSocket(int outIdx) {
    WiFiUDP& udp = sacnUdp[outIdx];
    for (int guard = 0; guard < 16; guard++) {
        int pktLen = udp.parsePacket();
        if (pktLen <= 0) return;
        if (pktLen < SACN_MIN_LEN) { udp.read(sacnBuf, sizeof(sacnBuf)); continue; }
        uint32_t senderIp = (uint32_t)udp.remoteIP();
        int n = udp.read(sacnBuf, sizeof(sacnBuf));
        if (n < SACN_MIN_LEN) continue;
        if (memcmp(sacnBuf + SACN_ACN_ID_OFF, ACN_PACKET_ID, 12) != 0) continue;
        uint32_t rootVec = ((uint32_t)sacnBuf[SACN_ROOT_VEC_OFF    ] << 24)
                         | ((uint32_t)sacnBuf[SACN_ROOT_VEC_OFF + 1] << 16)
                         | ((uint32_t)sacnBuf[SACN_ROOT_VEC_OFF + 2] <<  8)
                         |  (uint32_t)sacnBuf[SACN_ROOT_VEC_OFF + 3];
        if (rootVec != 0x00000004u) continue;
        uint32_t frameVec = ((uint32_t)sacnBuf[SACN_FRAME_VEC_OFF    ] << 24)
                           | ((uint32_t)sacnBuf[SACN_FRAME_VEC_OFF + 1] << 16)
                           | ((uint32_t)sacnBuf[SACN_FRAME_VEC_OFF + 2] <<  8)
                           |  (uint32_t)sacnBuf[SACN_FRAME_VEC_OFF + 3];
        if (frameVec != 0x00000002u) continue;
        uint16_t universe = ((uint16_t)sacnBuf[SACN_UNIVERSE_OFF] << 8)
                          | sacnBuf[SACN_UNIVERSE_OFF + 1];
        if (sacnBuf[SACN_STARTCODE_OFF] != 0x00) continue;
        uint8_t priority = sacnBuf[SACN_PRIORITY_OFF];
        routeFrame((int)universe - 1, sacnBuf + SACN_DATA_OFF, 512, senderIp, 1, priority);
    }
}

void readSacn() {
    for (int i = 0; i < MAX_OUTPUTS; i++)
        if (cfg.outputs[i].enabled) readSacnSocket(i);
}
