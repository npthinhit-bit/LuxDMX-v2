#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern TaskHandle_t g_dmxTask;

extern uint32_t dmxPeriodMs(int out);
extern uint8_t  dmxRateHz(int out);

void createTasks();
void dmxInitGuardBegin();
void dmxInitGuardEnd();
void mergeOutputTimed();
void updateLedFromNet();
void renderDisplay();

extern TickType_t xLastWakeTime;

[[noreturn]] void dmxTxTask(void*);
[[noreturn]] void netRxTask(void*);
[[noreturn]] void ledTask(void*);
[[noreturn]] void displayTask(void*);
[[noreturn]] void versionCheckTask(void*);
