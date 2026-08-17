#pragma once
// DE/RE GPIO control for half-duplex RS485 transceivers.
// HIGH = drive (TX), LOW = listen (RX). A separate RX-only UART
// (see uart_rx.h) catches the responder's reply, so the RMT TX channel
// is never released -- DMX output on this pin is uninterrupted between
// RDM transactions (issue #64).
#include "driver/gpio.h"

static inline void gpioDeInit(int pin)
{
    if (pin < 0)
        return;
    gpio_config_t io = {};
    io.pin_bit_mask  = 1ULL << pin;
    io.mode          = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    gpio_set_level((gpio_num_t)pin, 1);  // idle in drive so RMT DMX keeps clocking
}

static inline void gpioDeSet(int pin, int level)
{
    gpio_set_level((gpio_num_t)pin, level);
}
