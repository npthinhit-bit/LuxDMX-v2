/*
 * Native test shim - FreeRTOS semaphores
 */
#pragma once

typedef void* SemaphoreHandle_t;
typedef int BaseType_t;
typedef unsigned long UBaseType_t;

#define pdTRUE 1
#define pdFALSE 0

SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, unsigned long xTicksToWait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore);
