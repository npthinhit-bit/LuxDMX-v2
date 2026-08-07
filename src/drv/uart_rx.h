#pragma once
// UART RX primitive for RDM responses. Configured 8N2 at 250000 baud
// (matching DMX512 timing). RX-only: TX/RTS/CTS pins are left unconnected
// so the UART never drives the bus. The response reader in rdm_engine
// consumes frames from this UART.
#include "driver/uart.h"
#include "driver/gpio.h"

static inline bool uartRxInit(uart_port_t uart, int rxPin) {
    if (rxPin < 0) return false;
    uart_config_t uc = {};
    uc.baud_rate   = 250000;           // DMX/RDM line rate
    uc.data_bits   = UART_DATA_8_BITS;
    uc.parity      = UART_PARITY_DISABLE;
    uc.stop_bits   = UART_STOP_BITS_2;  // 8N2
    uc.flow_ctrl   = UART_HW_FLOWCTRL_DISABLE;
    uc.source_clk  = UART_SCLK_DEFAULT;
    if (uart_driver_install(uart, 512, 0, 0, nullptr, 0) != ESP_OK) return false;
    uart_param_config(uart, &uc);
    // RX only -- leave TX/RTS/CTS on PIN_NO_CHANGE so the UART never drives the bus.
    uart_set_pin(uart, UART_PIN_NO_CHANGE, rxPin,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    gpio_set_pull_mode((gpio_num_t)rxPin, GPIO_PULLUP_ONLY);
    return true;
}

static inline void uartRxFlush(uart_port_t uart) {
    uart_flush_input(uart);
}

// Read up to maxLen bytes from the UART RX FIFO, blocking up to timeoutMs.
// Returns the number of bytes read (0 on timeout / nothing available).
static inline int uartRxRead(uart_port_t uart, uint8_t* buf, int maxLen, int timeoutMs) {
    return uart_read_bytes(uart, buf, maxLen, pdMS_TO_TICKS(timeoutMs));
}
