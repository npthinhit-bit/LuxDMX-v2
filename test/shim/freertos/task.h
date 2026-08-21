/*
 * Native test shim - FreeRTOS task
 */
#pragma once

#include "FreeRTOS.h"

void vTaskDelay(unsigned long xTicksToDelay);
void vTaskDelayUntil(unsigned long* pxPreviousWakeTime, unsigned long xTimeIncrement);

#define pdMS_TO_TICKS(ms) (ms)
#define portTICK_PERIOD_MS 1

typedef void (*TaskFunction_t)(void*);
typedef void* TaskHandle_t;

void xTaskCreate(TaskFunction_t pvTaskCode, const char* const pcName,
                 unsigned short usStackDepth, void* pvParameters,
                 unsigned char ucPriority, TaskHandle_t* pvCreatedTask);
void vTaskDelete(TaskHandle_t xTask);
void vTaskDelay(unsigned long xTicksToDelay);
