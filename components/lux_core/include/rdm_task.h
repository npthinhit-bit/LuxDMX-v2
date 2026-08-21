#pragma once

#include "rdm_engine.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RDM_TASK_QUEUE_CAPACITY 32u
#define RDM_TASK_PRIORITY 18u
#define RDM_TASK_STACK_SIZE 8192u

bool rdmTaskInit(void);
void rdmTaskDeinit(void);
bool rdmTransactionAsync(const rdm_request_t *request);

#ifdef __cplusplus
}
#endif
