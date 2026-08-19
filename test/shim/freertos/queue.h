/*
 * Native test shim - FreeRTOS queue
 */
#pragma once

#include "FreeRTOS.h"

typedef void* QueueHandle_t;
typedef void* StaticQueue_t;

#define queueSEND_TO_BACK 0
#define queueRECEIVE 1
