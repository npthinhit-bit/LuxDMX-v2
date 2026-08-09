#pragma once
// DMX input via UART break detection (E1.11 receiver).
// Configured 8N2 at 250000 baud; a DMX break (>=88us low) causes the UART
// to generate a break interrupt. The ISR reads the post-break slots from the
// RX FIFO and assembles a complete DMX frame.
#include "driver/uart.h"
#include "driver/gpio.h"
#include "rdm_types.h"

#define DMX_IN_BUF_SIZE DMX_PACKET_SIZE

struct DmxInFrame {
    uint8_t  data[DMX_PACKET_SIZE];  // start code + 512 slots
    uint16_t len;                    // valid bytes in data[]
    uint32_t ts;                     // millis() when frame was completed
    bool     valid;
};

extern DmxInFrame g_dmxInFrame;
extern volatile bool   g_dmxInFrameReady;

bool dmxInInit(int uartNum, int rxPin);
bool dmxInPoll(int uartNum);
