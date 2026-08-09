// DMX input via UART polling with break detection.
// The ESP32 UART in 8N2@250kbaud sees a DMX break (line low for >=88us) as a
// sequence of 0 bits. On break, the UART's RX FIFO is flushed (break is treated
// as noise/framing error). After the MAB (mark = idle high), the start code +
// 512 slots arrive as a normal 8N2 byte stream. We poll the UART with a short
// timeout; a read timeout between bytes indicates the end of a frame boundary,
// and the first byte after a break is the start code.
#include "dmx_input.h"
#include <Arduino.h>
#include <string.h>

DmxInFrame g_dmxInFrame;
volatile bool   g_dmxInFrameReady = false;

static uint16_t g_dmxInIdx = 0;
static uint32_t g_dmxInLastByteMs = 0;
static bool     g_dmxInFrameStart = false;

bool dmxInInit(int uartNum, int rxPin) {
    if (rxPin < 0 || uartNum < 1 || uartNum > 2) return false;
    uart_port_t uart = (uart_port_t)(uartNum - 1);  // UART1/UART2
    uart_config_t uc = {};
    uc.baud_rate   = 250000;
    uc.data_bits   = UART_DATA_8_BITS;
    uc.parity      = UART_PARITY_DISABLE;
    uc.stop_bits   = UART_STOP_BITS_2;
    uc.flow_ctrl   = UART_HW_FLOWCTRL_DISABLE;
    uc.source_clk  = UART_SCLK_DEFAULT;
    if (uart_driver_install(uart, DMX_IN_BUF_SIZE + 16, 0, 1, nullptr, 0) != ESP_OK)
        return false;
    uart_param_config(uart, &uc);
    uart_set_pin(uart, UART_PIN_NO_CHANGE, rxPin,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    gpio_set_pull_mode((gpio_num_t)rxPin, GPIO_PULLUP_ONLY);
    return true;
}

bool dmxInPoll(int uartNum) {
    if (uartNum < 1 || uartNum > 2) return false;
    uart_port_t uart = (uart_port_t)(uartNum - 1);
    uint32_t now = millis();

    if (g_dmxInFrameStart && g_dmxInIdx > 0 && (uint32_t)(now - g_dmxInLastByteMs) > 2) {
        if (g_dmxInIdx >= 2) {
            g_dmxInFrame.len = g_dmxInIdx;
            g_dmxInFrame.ts = now;
            g_dmxInFrame.valid = true;
            g_dmxInFrameReady = true;
        }
        g_dmxInIdx = 0;
        g_dmxInFrameStart = false;
        return g_dmxInFrameReady;
    }

    uint8_t buf[64];
    int len = uart_read_bytes(uart, buf, sizeof(buf), 0);
    if (len > 0) {
        if (!g_dmxInFrameStart) {
            g_dmxInFrame.data[0] = buf[0];
            g_dmxInIdx = 1;
            g_dmxInFrameStart = true;
            g_dmxInLastByteMs = now;
            for (int i = 1; i < len && g_dmxInIdx < DMX_PACKET_SIZE; i++) {
                g_dmxInFrame.data[g_dmxInIdx++] = buf[i];
                g_dmxInLastByteMs = now;
            }
        } else {
            for (int i = 0; i < len && g_dmxInIdx < DMX_PACKET_SIZE; i++) {
                g_dmxInFrame.data[g_dmxInIdx++] = buf[i];
                g_dmxInLastByteMs = now;
            }
        }
    }

    if (g_dmxInIdx >= DMX_PACKET_SIZE) {
        g_dmxInFrame.len = DMX_PACKET_SIZE;
        g_dmxInFrame.ts = now;
        g_dmxInFrame.valid = true;
        g_dmxInFrameReady = true;
        g_dmxInIdx = 0;
        g_dmxInFrameStart = false;
        return true;
    }
    return false;
}
