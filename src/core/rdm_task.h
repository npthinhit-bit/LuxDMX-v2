#pragma once
// Dedicated RDM task + command queue to offload rdmTransaction() from DMX TX task.
// This prevents the ~3ms blocking per RDM call on core-1.

#include "rdm_engine.h"
#include "rdm_types.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#define RDM_TASK_STACK_SIZE 8192
#define RDM_TASK_PRIORITY 18
#define RDM_QUEUE_LENGTH 32

enum RdmCmdType
{
    RDM_CMD_TRANSACTION,
    RDM_CMD_DISCOVER,
    RDM_CMD_MUTE,
    RDM_CMD_UNMUTE_ALL,
    RDM_CMD_SELECT_LINE,
    RDM_CMD_RAW_RELAY,
};

struct RdmCmd
{
    RdmCmdType type;
    // For transaction
    rdm_uid_t  dest;
    uint8_t    cc;
    uint16_t   pid;
    uint8_t    reqPd[32];
    uint8_t    reqPdl;
    uint8_t*   respPd;
    int        respMax;
    int*       respPdl;
    rdm_ack_t* ack;
    // For discover
    rdm_uid_t* found;
    int        maxFound;
    int*       foundCount;
    // For mute
    rdm_uid_t muteUid;
    // For select line
    int lineIdx;
    // For raw relay (legacy blocking path)
    const uint8_t* reqNoSC;
    int            reqLen;
    uint8_t*       respNoSC;
    int            respNoSCMax;
    int*           rawRelayResult;
    // For Art-Net async relay (core 0 -> core 1 -> core 0 response ring).
    // req is COPIED into the queue-owned buffer so the enqueue is truly
    // non-blocking: the core-0 caller's packet buffer must not be referenced
    // while core 1 runs the (multi-ms) RMT TX/RX transaction.
    uint8_t  artReq[261];
    uint16_t artReqLen;
    uint32_t artDestIp;
    // Completion
    SemaphoreHandle_t done;
    bool*             result;
};

struct RdmTaskState
{
    QueueHandle_t cmdQueue   = nullptr;
    TaskHandle_t  taskHandle = nullptr;
    bool          running    = false;
    // Discovery state
    bool              discRunning = false;
    rdm_uid_t*        discOut     = nullptr;
    int               discMax     = 0;
    int               discCount   = 0;
    SemaphoreHandle_t discDone    = nullptr;
};

extern RdmTaskState g_rdmTask;

bool rdmTaskInit();
void rdmTaskDeinit();

// Non-blocking API: returns immediately, result available via semaphore
bool rdmTransactionAsync(const rdm_uid_t& dest, uint8_t cc, uint16_t pid, const uint8_t* reqPd, uint8_t reqPdl,
                         uint8_t* respPd, int respMax, int* respPdl, rdm_ack_t* ack, SemaphoreHandle_t done,
                         bool* result);

bool rdmDiscoverAsync(rdm_uid_t* out, int max, int* count, SemaphoreHandle_t done, bool* result);
bool rdmMuteAsync(const rdm_uid_t& uid, SemaphoreHandle_t done, bool* result);
bool rdmUnMuteAllAsync(SemaphoreHandle_t done, bool* result);
bool rdmSelectLineAsync(int lineIdx, SemaphoreHandle_t done, bool* result);
bool rdmRawRelayAsync(const uint8_t* reqNoSC, int reqLen, uint8_t* respNoSC, int respMax, int* result,
                      SemaphoreHandle_t done, bool* success);

// Art-Net async relay: enqueue an RDM request to the core-1 DMX task without
// blocking core 0. The task runs the timing-critical RMT TX/RX and pushes the
// reply to the Art-Net response ring (art_rdm_resp_queue); netRxTask sends it via
// artRdmDrainResponses(). Returns false if the task/queue is down or full.
bool rdmArtRawRelayEnqueue(const uint8_t* reqNoSC, uint16_t reqLen, uint32_t destIp, int lineIdx);

// Blocking wrappers (for migration compatibility)
bool rdmTransaction(const rdm_uid_t& dest, uint8_t cc, uint16_t pid, const uint8_t* reqPd, uint8_t reqPdl,
                    uint8_t* respPd, int respMax, int* respPdl, rdm_ack_t* ack);
int  rdmRmtDiscover(rdm_uid_t* out, int max);
bool rdmMute(const rdm_uid_t& uid);
void rdmUnMuteAll();
void rdmRmtSelect(int line);
int  rdmRmtRawRelay(const uint8_t* reqNoSC, int reqLen, uint8_t* respNoSC, int respMax);