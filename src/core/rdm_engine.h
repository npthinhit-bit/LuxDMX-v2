#pragma once
// RMT-TX + UART-RX RDM (E1.20) controller transport layer.
// RDM requests go out over RMT (same channel as DMX), responses come back on a
// RX-only UART, DE/RE is a GPIO. Nothing is installed/deleted at runtime, so
// there is no interrupt leak and the RDM output keeps full RMT immunity.
#include "rdm_types.h"
#include "dmx_rmt.h"
#include "uart_rx.h"
#include "gpio_dir.h"
#include "config_schema.h"

#ifndef RDM_MAX_LINES
#define RDM_MAX_LINES 2
#endif

#define RDM_SC             0xCC
#define RDM_SC_SUB         0x01
#define RDM_HDR_LEN        24
#define RDM_RESP_TIMEOUT_MS 9
#define RDM_DISC_TIMEOUT_MS 6
#define IDENTIFY_MS 1500

static const uart_port_t RDM_LINE_UART[RDM_MAX_LINES] = { UART_NUM_2, UART_NUM_1 };

struct RdmLine { RmtDmx* rmt; int de; int rx; uart_port_t uart; bool up; };

struct RdmState {
    rdm_uid_t   ctrl = {0x4C58, 0};
    RdmLine     lines[RDM_MAX_LINES] = {};
    int         lineN = 0;
    RmtDmx*     rmt = nullptr;
    int         de = -1;
    int         rx = -1;
    uart_port_t uart = UART_NUM_2;
    uint8_t     tn = 0;
    volatile uint32_t sent = 0;
    volatile uint32_t recv = 0;
    volatile uint32_t sentMs = 0;
    volatile uint32_t recvMs = 0;
};

extern RdmState g_rdm;

// Identify: temporarily force one channel on the wire to locate a fixture.
extern uint16_t identifyCh;
extern uint32_t identifyUntil;

// Accessors used by the app layer and the LED/display tasks.
inline int         rdmLineCount()     { return g_rdm.lineN; }
inline uint32_t    rdmSent()           { return g_rdm.sent; }
inline uint32_t    rdmRecv()           { return g_rdm.recv; }
inline uint32_t    rdmSentMs()         { return g_rdm.sentMs; }
inline uint32_t    rdmRecvMs()         { return g_rdm.recvMs; }
inline uart_port_t rdmActiveUart()    { return g_rdm.uart; }

// Point the engine at a line (its RMT channel + DE pin + RX UART).
void rdmRmtSelect(int line);

// Register an RDM line for an RDM-capable output. Returns the line index or -1.
int rdmRmtInit(RmtDmx* rmt, int dePin, int rxPin, uart_port_t uart = UART_NUM_MAX);

// --- transport primitives (used by rdm_disc.cpp too) --------
void putUid(uint8_t* p, const rdm_uid_t& u);
int  rdmBuild(uint8_t* buf, const rdm_uid_t& dest, uint8_t cc, uint16_t pid,
              const uint8_t* pd, uint8_t pdl);
void rdmTx(const uint8_t* pkt, int len);
int  rdmReadFrame(uint8_t* rx, int rxMax);
bool rdmReadResp(const rdm_uid_t& expectFrom, uint8_t* pd, int pdMax, int* pdl, rdm_ack_t* ack);
bool rdmTransaction(const rdm_uid_t& dest, uint8_t cc, uint16_t pid,
                    const uint8_t* reqPd, uint8_t reqPdl,
                    uint8_t* respPd, int respMax, int* respPdl, rdm_ack_t* ack);
int  rdmRmtRawRelay(const uint8_t* reqNoSC, int reqLen, uint8_t* respNoSC, int respMax);

// --- typed GET/SET wrappers (implemented in rdm_typed.cpp) --------------------
// --- discovery (implemented in rdm_disc.cpp) ----------------------------------
bool rdmOpDeviceInfo(const rdm_uid_t& uid, rdm_device_info_t* i, rdm_ack_t* a);
bool rdmOpSwLabel(const rdm_uid_t& uid, char* b, size_t n, rdm_ack_t* a);
bool rdmOpSensorDef(const rdm_uid_t& uid, uint8_t s, rdm_sensor_definition_t* d, rdm_ack_t* a);
bool rdmOpSensorVal(const rdm_uid_t& uid, uint8_t s, rdm_sensor_value_t* v, rdm_ack_t* a);
bool rdmOpSetAddr(const rdm_uid_t& uid, uint16_t addr, rdm_ack_t* a);
bool rdmOpSetIdentify(const rdm_uid_t& uid, bool on, rdm_ack_t* a);
bool rdmOpGetString(const rdm_uid_t& uid, uint16_t pid, char* buf, size_t len, rdm_ack_t* ack);
bool rdmOpSetString(const rdm_uid_t& uid, uint16_t pid, const char* s, rdm_ack_t* ack);
bool rdmOpSetPersonality(const rdm_uid_t& uid, uint8_t pers, rdm_ack_t* ack);
bool rdmOpGetSensorFull(const rdm_uid_t& uid, uint8_t sensorNum,
                        int16_t* present, int16_t* lo, int16_t* hi, int16_t* rec, rdm_ack_t* ack);
bool rdmOpGetStatus(const rdm_uid_t& uid, uint8_t statusType,
                    uint8_t* outType, uint16_t* outId, int16_t* outD1, int16_t* outD2,
                    int* outCount, rdm_ack_t* ack);

extern bool rdmPollDirty;
void rdmSavePoll();
