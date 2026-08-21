#include "rdm_task.h"
#include "logger.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "rdm_task";

typedef struct {
    rdm_request_t request;
    uint8_t param_data[RDM_MAX_PARAM_DATA];
} rdm_task_command_t;

static QueueHandle_t s_queue = NULL;
static TaskHandle_t s_task = NULL;
static volatile bool s_running = false;

static void rdm_task_entry(void *argument)
{
    (void)argument;
    rdm_task_command_t command;
    uint8_t frame[RDM_REQUEST_MAX_SIZE];
    while (s_running) {
        if (xQueueReceive(s_queue, &command, pdMS_TO_TICKS(10)) != pdTRUE) {
            continue;
        }
        command.request.param_data = command.param_data;
        const size_t length = rdmBuild(frame, sizeof(frame), &command.request);
        if (length == 0) {
            LOG_WARN(TAG, "Rejected malformed RDM request");
        }
        /* Hardware transaction is intentionally owned by the drv layer. */
    }
    s_task = NULL;
    vTaskDelete(NULL);
}

bool rdmTaskInit(void)
{
    if (s_running && s_queue != NULL) return true;
    s_queue = xQueueCreate(RDM_TASK_QUEUE_CAPACITY, sizeof(rdm_task_command_t));
    if (s_queue == NULL) return false;
    s_running = true;
    BaseType_t created = xTaskCreatePinnedToCore(rdm_task_entry, "rdmTask",
                                                  RDM_TASK_STACK_SIZE, NULL,
                                                  RDM_TASK_PRIORITY, &s_task, 1);
    if (created != pdPASS) {
        s_running = false;
        vQueueDelete(s_queue);
        s_queue = NULL;
        return false;
    }
    return true;
}

void rdmTaskDeinit(void)
{
    s_running = false;
    if (s_task != NULL) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    if (s_queue != NULL) {
        vQueueDelete(s_queue);
        s_queue = NULL;
    }
}

bool rdmTransactionAsync(const rdm_request_t *request)
{
    if (!s_running || s_queue == NULL || request == NULL ||
        request->param_data_length > RDM_MAX_PARAM_DATA) {
        return false;
    }
    rdm_task_command_t command = {0};
    command.request = *request;
    if (request->param_data_length != 0 && request->param_data != NULL) {
        memcpy(command.param_data, request->param_data, request->param_data_length);
    }
    command.request.param_data = command.param_data;
    return xQueueSend(s_queue, &command, pdMS_TO_TICKS(100)) == pdTRUE;
}
