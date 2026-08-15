#include "rdm_task.h"
#include "rdm_engine.h"
#include "rdm_disc.h"
#include "art_rdm_resp_queue.h"
#include "uart_rx.h"
#include "gpio_dir.h"
#include "driver/rmt_tx.h"
#include <esp_rom_sys.h>
#include <Arduino.h>

// Global RDM task state
RdmTaskState g_rdmTask;

static void rdmTaskLoop(void* /*arg*/) {
    Serial.println("[RDM] task running on core 1 (priority 18)");
    RdmCmd cmd;

    while (g_rdmTask.running) {
        if (xQueueReceive(g_rdmTask.cmdQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
            bool success = false;

            switch (cmd.type) {
                case RDM_CMD_TRANSACTION: {
                    uint8_t pkt[64];
                    for (int attempt = 0; attempt < 3; attempt++) {
                        int len = rdmBuild(pkt, cmd.dest, cmd.cc, cmd.pid, cmd.reqPd, cmd.reqPdl);
                        rdmTx(pkt, len);
                        if (rdmReadResp(cmd.dest, cmd.respPd, cmd.respMax, cmd.respPdl, cmd.ack)) {
                            success = true;
                            break;
                        }
                        esp_rom_delay_us(1000);
                    }
                    break;
                }

                case RDM_CMD_DISCOVER: {
                    // Offload full discovery to RDM task
                    rdmUnMuteAll();
                    int count = 0;
                    rdmDiscRange(0, uidPack(RDM_UID_MAX), cmd.found, cmd.maxFound, &count);
                    if (cmd.foundCount) *cmd.foundCount = count;
                    success = (count > 0);
                    break;
                }

                case RDM_CMD_MUTE: {
                    // rdmMute() is the low-level DISC_MUTE primitive (rdm_disc.cpp),
                    // safe to call directly — we are already on the RDM task thread.
                    success = rdmMute(cmd.muteUid);
                    break;
                }

                case RDM_CMD_UNMUTE_ALL: {
                    uint8_t pkt[64];
                    int len = rdmBuild(pkt, RDM_UID_BROADCAST_ALL, RDM_CC_DISC_COMMAND,
                                       RDM_PID_DISC_UN_MUTE, nullptr, 0);
                    rdmTx(pkt, len);
                    vTaskDelay(pdMS_TO_TICKS(1));
                    uartRxFlush(g_rdm.uart);
                    success = true;
                    break;
                }

                case RDM_CMD_SELECT_LINE: {
                    if (cmd.lineIdx >= 0 && cmd.lineIdx < g_rdm.lineN) {
                        RdmLine& L = g_rdm.lines[cmd.lineIdx];
                        g_rdm.rmt  = L.rmt;
                        g_rdm.de   = L.de;
                        g_rdm.rx   = L.rx;
                        g_rdm.uart = L.uart;
                        success = true;
                    }
                    break;
                }

                case RDM_CMD_RAW_RELAY: {
                    if (cmd.artReqLen && cmd.lineIdx >= 0) rdmRmtSelect(cmd.lineIdx);
                    RmtDmx* rd = g_rdm.rmt;
                    if (rd && rd->chan) {
                        // Art-Net async path uses the cmd-owned copy; legacy
                        // blocking path uses the caller-provided pointers.
                        const uint8_t* req = cmd.artReqLen ? cmd.artReq : cmd.reqNoSC;
                        int reqLen = cmd.artReqLen ? (int)cmd.artReqLen : cmd.reqLen;
                        if (reqLen >= RDM_HDR_LEN - 1 && reqLen <= 260) {
                            static uint8_t pkt[264];
                            pkt[0] = RDM_SC;
                            memcpy(pkt + 1, req, reqLen);
                            uint16_t destMan = ((uint16_t)req[2] << 8) | req[3];
                            bool bcast = (destMan == 0xFFFF);

                            for (int attempt = 0; attempt < (bcast ? 1 : 3); attempt++) {
                                rdmTx(pkt, reqLen + 1);
                                if (bcast) {
                                    if (!cmd.artReqLen && cmd.rawRelayResult) *cmd.rawRelayResult = 0;
                                    success = true;
                                    break;
                                }
                                uint8_t rx[96];
                                int n = rdmReadFrame(rx, sizeof(rx));
                                gpioDeSet(g_rdm.de, 1);
                                if (n < 26) { esp_rom_delay_us(1000); continue; }
                                int s = -1;
                                for (int i = 0; i < n - 1; i++)
                                    if (rx[i] == RDM_SC && rx[i + 1] == RDM_SC_SUB) { s = i; break; }
                                if (s < 0) { esp_rom_delay_us(1000); continue; }
                                uint8_t* m = rx + s;
                                int avail = n - s;
                                int msgLen = m[2];
                                if (msgLen + 2 > avail || msgLen < RDM_HDR_LEN) { esp_rom_delay_us(1000); continue; }
                                uint16_t ck = 0; for (int i = 0; i < msgLen; i++) ck += m[i];
                                if (ck != (uint16_t)((m[msgLen] << 8) | m[msgLen + 1])) { esp_rom_delay_us(1000); continue; }
                                g_rdm.recv++;
                                g_rdm.recvMs = millis();
                                int outLen = msgLen + 2 - 1;

                                if (cmd.artReqLen) {
                                    // Art-Net async path: hand the no-SC reply to core 0.
                                    uint16_t pushLen = (uint16_t)outLen;
                                    if (pushLen > 256) pushLen = 256;
                                    artRdmPushResponse(cmd.artDestIp, m + 1, pushLen);
                                } else {
                                    if (outLen > cmd.respNoSCMax) outLen = cmd.respNoSCMax;
                                    memcpy(cmd.respNoSC, m + 1, outLen);
                                    if (cmd.rawRelayResult) *cmd.rawRelayResult = outLen;
                                }
                                success = true;
                                break;
                            }
                        }
                        break;
                    }
                }
            }

            if (cmd.result) *cmd.result = success;
            if (cmd.done) xSemaphoreGive(cmd.done);
        }
    }

    Serial.println("[RDM] task exiting");
    vTaskDelete(nullptr);
}

bool rdmTaskInit() {
    if (g_rdmTask.running) return true;

    g_rdmTask.cmdQueue = xQueueCreate(RDM_QUEUE_LENGTH, sizeof(RdmCmd));
    if (!g_rdmTask.cmdQueue) {
        Serial.println("[RDM] Failed to create command queue");
        return false;
    }

    g_rdmTask.running = true;
    BaseType_t res = xTaskCreatePinnedToCore(
        rdmTaskLoop, "rdm_task", RDM_TASK_STACK_SIZE, nullptr,
        RDM_TASK_PRIORITY, &g_rdmTask.taskHandle, 1
    );

    if (res != pdPASS) {
        Serial.println("[RDM] Failed to create task");
        vQueueDelete(g_rdmTask.cmdQueue);
        g_rdmTask.cmdQueue = nullptr;
        g_rdmTask.running = false;
        return false;
    }

    Serial.println("[RDM] task initialized");
    return true;
}

void rdmTaskDeinit() {
    if (!g_rdmTask.running) return;

    g_rdmTask.running = false;
    if (g_rdmTask.taskHandle) {
        vTaskDelete(g_rdmTask.taskHandle);
        g_rdmTask.taskHandle = nullptr;
    }
    if (g_rdmTask.cmdQueue) {
        vQueueDelete(g_rdmTask.cmdQueue);
        g_rdmTask.cmdQueue = nullptr;
    }
    if (g_rdmTask.discDone) {
        vSemaphoreDelete(g_rdmTask.discDone);
        g_rdmTask.discDone = nullptr;
    }
    Serial.println("[RDM] task deinitialized");
}

// Async API implementations
bool rdmTransactionAsync(const rdm_uid_t& dest, uint8_t cc, uint16_t pid,
                         const uint8_t* reqPd, uint8_t reqPdl,
                         uint8_t* respPd, int respMax, int* respPdl, rdm_ack_t* ack,
                         SemaphoreHandle_t done, bool* result) {
    if (!g_rdmTask.running || !g_rdmTask.cmdQueue) return false;

    RdmCmd cmd = {};
    cmd.type = RDM_CMD_TRANSACTION;
    cmd.dest = dest;
    cmd.cc = cc;
    cmd.pid = pid;
    cmd.reqPdl = reqPdl;
    cmd.respPd = respPd;
    cmd.respMax = respMax;
    cmd.respPdl = respPdl;
    cmd.ack = ack;
    cmd.done = done;
    cmd.result = result;
    if (reqPd && reqPdl > 0 && reqPdl <= 32) {
        memcpy(cmd.reqPd, reqPd, reqPdl);
    }

    return xQueueSend(g_rdmTask.cmdQueue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
}

bool rdmDiscoverAsync(rdm_uid_t* out, int max, int* count, SemaphoreHandle_t done, bool* result) {
    if (!g_rdmTask.running || !g_rdmTask.cmdQueue) return false;

    RdmCmd cmd = {};
    cmd.type = RDM_CMD_DISCOVER;
    cmd.found = out;
    cmd.maxFound = max;
    cmd.foundCount = count;
    cmd.done = done;
    cmd.result = result;

    return xQueueSend(g_rdmTask.cmdQueue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
}

bool rdmMuteAsync(const rdm_uid_t& uid, SemaphoreHandle_t done, bool* result) {
    if (!g_rdmTask.running || !g_rdmTask.cmdQueue) return false;

    RdmCmd cmd = {};
    cmd.type = RDM_CMD_MUTE;
    cmd.muteUid = uid;
    cmd.done = done;
    cmd.result = result;

    return xQueueSend(g_rdmTask.cmdQueue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
}

bool rdmUnMuteAllAsync(SemaphoreHandle_t done, bool* result) {
    if (!g_rdmTask.running || !g_rdmTask.cmdQueue) return false;

    RdmCmd cmd = {};
    cmd.type = RDM_CMD_UNMUTE_ALL;
    cmd.done = done;
    cmd.result = result;

    return xQueueSend(g_rdmTask.cmdQueue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
}

bool rdmSelectLineAsync(int lineIdx, SemaphoreHandle_t done, bool* result) {
    if (!g_rdmTask.running || !g_rdmTask.cmdQueue) return false;

    RdmCmd cmd = {};
    cmd.type = RDM_CMD_SELECT_LINE;
    cmd.lineIdx = lineIdx;
    cmd.done = done;
    cmd.result = result;

    return xQueueSend(g_rdmTask.cmdQueue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
}

bool rdmRawRelayAsync(const uint8_t* reqNoSC, int reqLen,
                      uint8_t* respNoSC, int respMax,
                      int* result, SemaphoreHandle_t done, bool* success) {
    if (!g_rdmTask.running || !g_rdmTask.cmdQueue) return false;

    RdmCmd cmd = {};
    cmd.type = RDM_CMD_RAW_RELAY;
    cmd.reqNoSC = reqNoSC;
    cmd.reqLen = reqLen;
    cmd.respNoSC = respNoSC;
    cmd.respMax = respMax;
    cmd.rawRelayResult = result;
    cmd.done = done;
    cmd.result = success;

    return xQueueSend(g_rdmTask.cmdQueue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
}


bool rdmArtRawRelayEnqueue(const uint8_t* reqNoSC, uint16_t reqLen, uint32_t destIp, int lineIdx) {
    if (!g_rdmTask.running || !g_rdmTask.cmdQueue) return false;
    RdmCmd cmd = {};
    cmd.type = RDM_CMD_RAW_RELAY;
    cmd.artDestIp = destIp;
    if (reqLen > (uint16_t)sizeof(cmd.artReq)) reqLen = (uint16_t)sizeof(cmd.artReq);
    if (reqLen > 0 && reqNoSC) {
        memcpy(cmd.artReq, reqNoSC, reqLen);
        cmd.artReqLen = reqLen;
    }
    cmd.lineIdx = lineIdx;
    return xQueueSend(g_rdmTask.cmdQueue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
}

// Blocking wrappers (for migration compatibility - call these from non-DMX tasks)
static bool waitForDone(SemaphoreHandle_t done, TickType_t timeout) {
    return xSemaphoreTake(done, timeout) == pdTRUE;
}

bool rdmTransaction(const rdm_uid_t& dest, uint8_t cc, uint16_t pid,
                    const uint8_t* reqPd, uint8_t reqPdl,
                    uint8_t* respPd, int respMax, int* respPdl, rdm_ack_t* ack) {
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    bool result = false;

    if (!rdmTransactionAsync(dest, cc, pid, reqPd, reqPdl, respPd, respMax, respPdl, ack, done, &result)) {
        vSemaphoreDelete(done);
        return false;
    }

    waitForDone(done, pdMS_TO_TICKS(5000));
    vSemaphoreDelete(done);
    return result;
}

int rdmRmtDiscover(rdm_uid_t* out, int max) {
    // Clamp to the configured responder ceiling (preserves pre-migration behaviour).
    int limit = max;
    if (cfg.rdmMaxDev > 0 && cfg.rdmMaxDev < limit) limit = cfg.rdmMaxDev;

    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    int count = 0;
    bool result = false;

    if (!rdmDiscoverAsync(out, limit, &count, done, &result)) {
        vSemaphoreDelete(done);
        return 0;
    }

    waitForDone(done, pdMS_TO_TICKS(30000)); // 30s budget for full discovery
    vSemaphoreDelete(done);
    return result ? count : 0;
}

int rdmRmtRawRelay(const uint8_t* reqNoSC, int reqLen, uint8_t* respNoSC, int respMax) {
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    int result = -1;
    bool success = false;

    if (!rdmRawRelayAsync(reqNoSC, reqLen, respNoSC, respMax, &result, done, &success)) {
        vSemaphoreDelete(done);
        return -1;
    }

    waitForDone(done, pdMS_TO_TICKS(5000));
    vSemaphoreDelete(done);
    return success ? result : -1;
}