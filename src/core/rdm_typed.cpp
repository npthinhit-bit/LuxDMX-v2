#include "rdm_engine.h"

bool rdmOpDeviceInfo(const rdm_uid_t& uid, rdm_device_info_t* i, rdm_ack_t* a) {
    uint8_t pd[40]; int pdl = 0;
    if (!rdmTransaction(uid, RDM_CC_GET_COMMAND, RDM_PID_DEVICE_INFO, nullptr, 0,
                        pd, sizeof(pd), &pdl, a)) return false;
    if (a->type != RDM_RESPONSE_TYPE_ACK || pdl < 19) return false;
    i->model_id            = (pd[2] << 8) | pd[3];
    i->product_category    = (pd[4] << 8) | pd[5];
    i->software_version_id = ((uint32_t)pd[6] << 24) | (pd[7] << 16) | (pd[8] << 8) | pd[9];
    i->footprint           = (pd[10] << 8) | pd[11];
    i->personality.current = pd[12];
    i->personality.count   = pd[13];
    i->dmx_start_address   = (pd[14] << 8) | pd[15];
    i->sub_device_count    = (pd[16] << 8) | pd[17];
    i->sensor_count        = pd[18];
    return true;
}

bool rdmOpSwLabel(const rdm_uid_t& uid, char* b, size_t n, rdm_ack_t* a) {
    uint8_t pd[40]; int pdl = 0;
    if (n) b[0] = 0;
    if (!rdmTransaction(uid, RDM_CC_GET_COMMAND, RDM_PID_SOFTWARE_VERSION_LABEL, nullptr, 0,
                        pd, sizeof(pd), &pdl, a)) return false;
    if (a->type != RDM_RESPONSE_TYPE_ACK) return false;
    int cn = pdl; if (cn > (int)n - 1) cn = n - 1; if (cn < 0) cn = 0;
    memcpy(b, pd, cn); b[cn] = 0;
    return true;
}

bool rdmOpSensorDef(const rdm_uid_t& uid, uint8_t s, rdm_sensor_definition_t* d, rdm_ack_t* a) {
    uint8_t pd[40]; int pdl = 0;
    if (!rdmTransaction(uid, RDM_CC_GET_COMMAND, RDM_PID_SENSOR_DEFINITION, &s, 1,
                        pd, sizeof(pd), &pdl, a)) return false;
    if (a->type != RDM_RESPONSE_TYPE_ACK || pdl < 13) return false;
    d->num  = pd[0]; d->type = pd[1]; d->unit = pd[2]; d->prefix = pd[3];
    int dn = pdl - 13; if (dn < 0) dn = 0; if (dn > 32) dn = 32;
    memcpy(d->description, pd + 13, dn); d->description[dn] = 0;
    return true;
}

bool rdmOpSensorVal(const rdm_uid_t& uid, uint8_t s, rdm_sensor_value_t* v, rdm_ack_t* a) {
    uint8_t pd[16]; int pdl = 0;
    if (!rdmTransaction(uid, RDM_CC_GET_COMMAND, RDM_PID_SENSOR_VALUE, &s, 1,
                        pd, sizeof(pd), &pdl, a)) return false;
    if (a->type != RDM_RESPONSE_TYPE_ACK || pdl < 3) return false;
    v->sensor_num    = pd[0];
    v->present_value = (int16_t)((pd[1] << 8) | pd[2]);
    return true;
}

bool rdmOpSetAddr(const rdm_uid_t& uid, uint16_t addr, rdm_ack_t* a) {
    uint8_t pd[2] = { (uint8_t)(addr >> 8), (uint8_t)(addr & 0xff) };
    uint8_t resp[8]; int rpdl = 0;
    if (!rdmTransaction(uid, RDM_CC_SET_COMMAND, RDM_PID_DMX_START_ADDRESS, pd, 2,
                        resp, sizeof(resp), &rpdl, a)) return false;
    return a->type == RDM_RESPONSE_TYPE_ACK;
}

bool rdmOpSetIdentify(const rdm_uid_t& uid, bool on, rdm_ack_t* a) {
    uint8_t pd = on ? 1 : 0;
    uint8_t resp[8]; int rpdl = 0;
    if (!rdmTransaction(uid, RDM_CC_SET_COMMAND, RDM_PID_IDENTIFY_DEVICE, &pd, 1,
                        resp, sizeof(resp), &rpdl, a)) return false;
    return a->type == RDM_RESPONSE_TYPE_ACK;
}

bool rdmOpGetString(const rdm_uid_t& uid, uint16_t pid, char* buf, size_t len, rdm_ack_t* ack) {
    uint8_t pd[40]; int pdl = 0;
    if (len) buf[0] = 0;
    if (!rdmTransaction(uid, RDM_CC_GET_COMMAND, pid, nullptr, 0, pd, sizeof(pd), &pdl, ack)) return false;
    if (ack->type != RDM_RESPONSE_TYPE_ACK) return false;
    int n = pdl; if (n > (int)len - 1) n = len - 1; if (n < 0) n = 0;
    memcpy(buf, pd, n); buf[n] = 0;
    return true;
}

bool rdmOpSetString(const rdm_uid_t& uid, uint16_t pid, const char* s, rdm_ack_t* ack) {
    uint8_t resp[8]; int rpdl = 0;
    int n = (int)strlen(s); if (n > 32) n = 32;
    if (!rdmTransaction(uid, RDM_CC_SET_COMMAND, pid, (const uint8_t*)s, (uint8_t)n, resp, sizeof(resp), &rpdl, ack))
        return false;
    return ack->type == RDM_RESPONSE_TYPE_ACK;
}

bool rdmOpSetPersonality(const rdm_uid_t& uid, uint8_t pers, rdm_ack_t* ack) {
    uint8_t resp[8]; int rpdl = 0;
    if (!rdmTransaction(uid, RDM_CC_SET_COMMAND, RDM_PID_DMX_PERSONALITY, &pers, 1, resp, sizeof(resp), &rpdl, ack))
        return false;
    return ack->type == RDM_RESPONSE_TYPE_ACK;
}

bool rdmOpGetSensorFull(const rdm_uid_t& uid, uint8_t sensorNum,
                        int16_t* present, int16_t* lo, int16_t* hi, int16_t* rec, rdm_ack_t* ack) {
    uint8_t pd[16]; int pdl = 0;
    if (!rdmTransaction(uid, RDM_CC_GET_COMMAND, RDM_PID_SENSOR_VALUE, &sensorNum, 1, pd, sizeof(pd), &pdl, ack))
        return false;
    if (ack->type != RDM_RESPONSE_TYPE_ACK || pdl < 3) return false;
    *present = (int16_t)((pd[1] << 8) | pd[2]);
    *lo  = pdl >= 5 ? (int16_t)((pd[3] << 8) | pd[4]) : *present;
    *hi  = pdl >= 7 ? (int16_t)((pd[5] << 8) | pd[6]) : *present;
    *rec = pdl >= 9 ? (int16_t)((pd[7] << 8) | pd[8]) : *present;
    return true;
}

bool rdmOpGetStatus(const rdm_uid_t& uid, uint8_t statusType,
                    uint8_t* outType, uint16_t* outId, int16_t* outD1, int16_t* outD2,
                    int* outCount, rdm_ack_t* ack) {
    uint8_t pd[80]; int pdl = 0;
    *outType = 0; *outId = 0; *outD1 = 0; *outD2 = 0; *outCount = 0;
    if (!rdmTransaction(uid, RDM_CC_GET_COMMAND, RDM_PID_STATUS_MESSAGE, &statusType, 1,
                        pd, sizeof(pd), &pdl, ack)) return false;
    if (ack->type != RDM_RESPONSE_TYPE_ACK) return false;
    int nmsg = pdl / 9;
    *outCount = nmsg;
    int best = -1; uint8_t bestType = 0;
    for (int i = 0; i < nmsg; i++) {
        uint8_t t = pd[i * 9 + 2];
        if (best < 0 || t > bestType) { bestType = t; best = i; }
    }
    if (best >= 0) {
        const uint8_t* m = pd + best * 9;
        *outType = m[2];
        *outId   = (uint16_t)((m[3] << 8) | m[4]);
        *outD1   = (int16_t)((m[5] << 8) | m[6]);
        *outD2   = (int16_t)((m[7] << 8) | m[8]);
    }
    return true;
}

int rdmSubDeviceCount(const rdm_uid_t& uid) {
    rdm_device_info_t info;
    rdm_ack_t ack;
    if (!rdmOpDeviceInfo(uid, &info, &ack)) return -1;
    return (int)info.sub_device_count;
}
