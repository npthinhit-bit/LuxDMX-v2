#include "rdm_engine.h"
#include "rdm_task.h"
#include "driver/rmt_tx.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "Preferences.h"
#include "stats.h"
#include <esp_rom_sys.h>
#include <esp_mac.h>

// --- module state -------------------------------------------------------------

RdmState g_rdm;
bool rdmPollDirty = false;
uint16_t identifyCh    = 0;
uint32_t identifyUntil = 0;

void rdmSavePoll() {
    Preferences p;
    p.begin("dmxgw", false);
    p.putUChar("rdmcount", (uint8_t)stats().rdmCount);
    p.putULong("rdmsent", g_rdm.sent);
    p.putULong("rdmrecv", g_rdm.recv);
    p.end();
}

void rdmInitCtrlUid() {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    g_rdm.ctrl.dev_id = ((uint32_t)mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5];
}

// --- line management ----------------------------------------------------------

void rdmRmtSelect(int line) {
    if (line < 0 || line >= g_rdm.lineN) return;
    RdmLine& L = g_rdm.lines[line];
    g_rdm.rmt  = L.rmt;
    g_rdm.de   = L.de;
    g_rdm.rx   = L.rx;
    g_rdm.uart = L.uart;
}

int rdmRmtInit(RmtDmx* rmt, int dePin, int rxPin, uart_port_t uart) {
    if (g_rdm.lineN >= RDM_MAX_LINES) return -1;
    int idx = g_rdm.lineN;
    if (uart == UART_NUM_MAX)
        uart = RDM_LINE_UART[idx];
    if (idx == 0) {
        rdmInitCtrlUid();
    }
    gpioDeInit(dePin);
    uartRxInit(uart, rxPin);

    g_rdm.lines[idx].rmt   = rmt;
    g_rdm.lines[idx].de    = dePin;
    g_rdm.lines[idx].rx    = rxPin;
    g_rdm.lines[idx].uart  = uart;
    g_rdm.lines[idx].up    = true;
    g_rdm.lineN++;
    if (idx == 0) rdmRmtSelect(0);
    return idx;
}

// --- request framing ----------------------------------------------------------

void putUid(uint8_t* p, const rdm_uid_t& u) {
    p[0] = u.man_id >> 8; p[1] = u.man_id & 0xff;
    p[2] = u.dev_id >> 24; p[3] = u.dev_id >> 16; p[4] = u.dev_id >> 8; p[5] = u.dev_id & 0xff;
}

int rdmBuild(uint8_t* buf, const rdm_uid_t& dest, uint8_t cc, uint16_t pid,
             const uint8_t* pd, uint8_t pdl) {
    int i = 0;
    buf[i++] = RDM_SC;
    buf[i++] = RDM_SC_SUB;
    buf[i++] = RDM_HDR_LEN + pdl;
    putUid(&buf[i], dest); i += 6;
    putUid(&buf[i], g_rdm.ctrl); i += 6;
    buf[i++] = g_rdm.tn++;
    buf[i++] = 0x01;
    buf[i++] = 0x00;
    buf[i++] = 0x00; buf[i++] = 0x00;
    buf[i++] = cc;
    buf[i++] = pid >> 8; buf[i++] = pid & 0xff;
    buf[i++] = pdl;
    for (uint8_t j = 0; j < pdl; j++) buf[i++] = pd[j];
    uint16_t ck = 0; for (int j = 0; j < i; j++) ck += buf[j];
    buf[i++] = ck >> 8; buf[i++] = ck & 0xff;
    return i;
}

// --- transmit / receive ------------------------------------------------------

void rdmTx(const uint8_t* pkt, int len) {
    RmtDmx* rd = g_rdm.rmt;
    if (!rd || !rd->chan) return;
    g_rdm.sent++;
    g_rdm.sentMs = millis();
    uartRxFlush(g_rdm.uart);
    gpioDeSet(g_rdm.de, 1);
    rmtDmxEncode(rd, pkt, len);
    rmt_transmit_config_t tc = {}; tc.loop_count = 0; tc.flags.eot_level = 1;
    rmt_transmit(rd->chan, rd->enc, rd->sym, rd->nsym * sizeof(rmt_symbol_word_t), &tc);
    rmt_tx_wait_all_done(rd->chan, 60);
    gpioDeSet(g_rdm.de, 0);
    esp_rom_delay_us(90);
    uartRxFlush(g_rdm.uart);
}

int rdmReadFrame(uint8_t* rx, int rxMax) {
    uint32_t t0 = micros();
    int n = 0, sc = -1, need = -1;
    while ((uint32_t)(micros() - t0) < (uint32_t)RDM_RESP_TIMEOUT_MS * 1000u) {
        int r = uartRxRead(g_rdm.uart, rx + n, rxMax - n, 1);
        if (r > 0) {
            n += r;
            if (sc < 0)
                for (int i = 0; i + 1 < n; i++)
                    if (rx[i] == RDM_SC && rx[i + 1] == RDM_SC_SUB) { sc = i; break; }
            if (sc >= 0 && need < 0 && n - sc >= 3)
                need = rx[sc + 2] + 2;
            if (need > 0 && n - sc >= need) break;
        }
        if (n >= rxMax) break;
    }
    return n;
}

bool rdmReadResp(const rdm_uid_t& expectFrom, uint8_t* pd, int pdMax, int* pdl,
                 rdm_ack_t* ack) {
    uint8_t rx[96];
    int n = rdmReadFrame(rx, sizeof(rx));
    gpioDeSet(g_rdm.de, 1);
    const char* fail = nullptr;
    int s = -1, msgLen = -1; uint16_t ck = 0, got = 0;
    if (n < 26) fail = "short";
    else {
        for (int i = 0; i < n - 1; i++)
            if (rx[i] == RDM_SC && rx[i + 1] == RDM_SC_SUB) { s = i; break; }
        if (s < 0) fail = "noSC";
    }
    uint8_t* m = rx + s;
    if (!fail) {
        int avail = n - s;
        msgLen = m[2];
        if (msgLen + 2 > avail || msgLen < RDM_HDR_LEN) fail = "len";
        else {
            for (int i = 0; i < msgLen; i++) ck += m[i];
            got = (m[msgLen] << 8) | m[msgLen + 1];
            if (ck != got) fail = "ck";
        }
    }
    if (fail) return false;
    if (ack) {
        ack->src_uid.man_id = (m[9] << 8) | m[10];
        ack->src_uid.dev_id = ((uint32_t)m[11] << 24) | (m[12] << 16) | (m[13] << 8) | m[14];
        ack->type          = (rdm_response_type_t)m[16];
        ack->message_count = m[17];
        ack->pid           = (m[21] << 8) | m[22];
        ack->err           = DMX_OK;
        ack->size          = msgLen + 2;
        ack->pdl           = m[23];
    }
    int rpdl = m[23];
    if (rpdl > pdMax) rpdl = pdMax;
    if (23 + rpdl > msgLen) rpdl = msgLen - 23;
    if (rpdl < 0) rpdl = 0;
    for (int i = 0; i < rpdl; i++) pd[i] = m[24 + i];
    *pdl = rpdl;
    g_rdm.recv++;
    g_rdm.recvMs = millis();
    return true;
}

// rdmTransaction() and rdmRmtRawRelay() moved to rdm_task.cpp
// These now execute on the dedicated RDM task (core 1, priority 18)
// to avoid blocking the DMX TX task.
