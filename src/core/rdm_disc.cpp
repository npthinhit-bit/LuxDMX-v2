#include "rdm_disc.h"
#include "rdm_engine.h"
#include "rdm_task.h"
#include "config_schema.h"
#include "driver/rmt_tx.h"
#include <esp_rom_sys.h>

uint64_t uidPack(const rdm_uid_t& u) {
    return ((uint64_t)u.man_id << 32) | u.dev_id;
}

rdm_uid_t uidUnpack(uint64_t v) {
    rdm_uid_t u; u.man_id = v >> 32; u.dev_id = (uint32_t)v; return u;
}

int rdmDiscBranch(uint64_t lower, uint64_t upper, rdm_uid_t* found) {
    uint8_t pd[12];
    putUid(&pd[0], uidUnpack(lower));
    putUid(&pd[6], uidUnpack(upper));
    bool sawBytes = false;
    for (int attempt = 0; attempt < 3; attempt++) {
        uint8_t pkt[64];
        int len = rdmBuild(pkt, RDM_UID_BROADCAST_ALL, RDM_CC_DISC_COMMAND,
                           RDM_PID_DISC_UNIQUE_BRANCH, pd, 12);
        rdmTx(pkt, len);
        uint8_t rx[48];
        int n = uartRxRead(g_rdm.uart, rx, sizeof(rx), RDM_DISC_TIMEOUT_MS);
        gpioDeSet(g_rdm.de, 1);
        if (n <= 0) { esp_rom_delay_us(200); continue; }
        sawBytes = true;
        int sep = -1;
        for (int i = 0; i < n; i++) {
            if (rx[i] == 0xAA) { sep = i; break; }
            if (rx[i] != 0xFE) break;
        }
        if (sep >= 0 && n - sep - 1 >= 16) {
            uint8_t* e = rx + sep + 1;
            uint8_t u[6];
            for (int i = 0; i < 6; i++) u[i] = e[i * 2] & e[i * 2 + 1];
            uint16_t csum = 0; for (int i = 0; i < 6; i++) csum += (uint16_t)u[i] + 0xFF;
            uint8_t ch[2] = { (uint8_t)(e[12] & e[13]), (uint8_t)(e[14] & e[15]) };
            if (csum == (uint16_t)((ch[0] << 8) | ch[1])) {
                found->man_id = (u[0] << 8) | u[1];
                found->dev_id = ((uint32_t)u[2] << 24) | (u[3] << 16) | (u[4] << 8) | u[5];
                return 1;
            }
        }
    }
    return sawBytes ? 2 : 0;
}

bool rdmMute(const rdm_uid_t& uid) {
    uint8_t resp[8]; int rpdl = 0; rdm_ack_t ack;
    for (int attempt = 0; attempt < 3; attempt++) {
        uint8_t pkt[64];
        int len = rdmBuild(pkt, uid, RDM_CC_DISC_COMMAND, RDM_PID_DISC_MUTE, nullptr, 0);
        rdmTx(pkt, len);
        if (rdmReadResp(uid, resp, sizeof(resp), &rpdl, &ack)) return true;
        esp_rom_delay_us(1000);
    }
    return false;
}

void rdmUnMuteAll() {
    uint8_t pkt[64];
    int len = rdmBuild(pkt, RDM_UID_BROADCAST_ALL, RDM_CC_DISC_COMMAND,
                       RDM_PID_DISC_UN_MUTE, nullptr, 0);
    rdmTx(pkt, len);
    vTaskDelay(pdMS_TO_TICKS(1));
    uartRxFlush(g_rdm.uart);
}

void rdmDiscRange(uint64_t lo0, uint64_t hi0, rdm_uid_t* out, int max, int* count) {
    static uint64_t stkLo[64], stkHi[64];
    int sp = 0; stkLo[sp] = lo0; stkHi[sp] = hi0; sp++;
    int budget = 8 * max + 128;
    while (sp > 0 && *count < max && budget > 0) {
        uint64_t lo = stkLo[--sp], hi = stkHi[sp];
        int guard = 0;
        while (*count < max && guard++ < max + 4 && budget-- > 0) {
            rdm_uid_t f;
            int r = rdmDiscBranch(lo, hi, &f);
            if (r == 0) break;
            if (r == 1) {
                if (rdmMute(f)) {
                    bool dup = false;
                    for (int i = 0; i < *count; i++)
                        if (uidPack(out[i]) == uidPack(f)) { dup = true; break; }
                    if (!dup) out[(*count)++] = f;
                }
                continue;
            }
            if (lo >= hi) break;
            uint64_t mid = lo + (hi - lo) / 2;
            if (sp < 63) { stkLo[sp] = mid + 1; stkHi[sp] = hi; sp++; }
            hi = mid;
        }
    }
}

// rdmRmtDiscover() now lives in rdm_task.cpp — it dispatches a discovery job to the
// dedicated RDM task (core 1, priority 18) so the DMX TX task is never blocked.
// rdm_disc.cpp retains only the low-level primitives (rdmDiscBranch, rdmDiscRange,
// rdmMute, rdmUnMuteAll) that the task handler invokes directly on core 1.
