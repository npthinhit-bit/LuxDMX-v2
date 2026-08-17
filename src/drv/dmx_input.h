#pragma once
// DMX input via UART break detection (E1.11 receiver).
// Configured 8N2 at 250000 baud; a DMX break (>=88 us low) is detected by the
// ESP32-S3 UART break detector (BRK_DET, primary frame-start marker) and the
// inter-byte timeout in dmxInPoll() acts as a frame-completion fallback.
// The post-break start code + 512 slots arrive as a normal 8N2 byte stream
// read from the RX FIFO.
#include "driver/gpio.h"
#include "driver/uart.h"
#include "rdm_types.h"

#define DMX_IN_BUF_SIZE DMX_PACKET_SIZE

struct DmxInFrame
{
    uint8_t  data[DMX_PACKET_SIZE];  // start code + 512 slots
    uint16_t len;                    // valid bytes in data[]
    uint32_t ts;                     // millis() when frame was completed
    bool     valid;
};

extern DmxInFrame    g_dmxInFrame;
extern volatile bool g_dmxInFrameReady;

bool dmxInInit(int uartNum, int rxPin);
bool dmxInPoll(int uartNum);