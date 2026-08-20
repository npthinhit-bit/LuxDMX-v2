/*
 * spec 18 - sACN (E1.31) Protocol Subsystem
 *
 * One UDP multicast socket per enabled output, bound to port 5568 and joined to
 * 239.255.<hi>.<lo> derived from the output's 1-based sACN universe
 * (hi = universe >> 8, lo = universe & 0xFF). sacn_poll() drains each socket up
 * to SACN_PER_SOCKET_LIMIT packets into a 638-byte buffer, validates the E1.31
 * root vector (0x04) and minimum size (638), then enqueues parsed packets.
 * sacn_dispatch_packet() decodes the frame vector:
 *   0x00000002 Streaming Data -> routeFrame / routeFrameNzs (PROTO_SACN)
 *   0x00000003 Stream Sync     -> flush staged frames
 */
#include "sacn.h"
#include "logger.h"
#include "frame_router.h"
#include "merge_engine.h"
#include "dmx_buffer.h"
#include "config_engine.h"
#include "esp_timer.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>

#ifndef PROTO_SACN
#define PROTO_SACN 1
#endif

#define SACN_FRAME_VEC_STREAM  0x00000002u
#define SACN_FRAME_VEC_SYNC    0x00000003u

#define BE32(p) (((uint32_t)(p)[0] << 24) | ((uint32_t)(p)[1] << 16) | \
                 ((uint32_t)(p)[2] << 8)  | ((uint32_t)(p)[3]))
#define LE16(p) ((uint16_t)(p)[0] | ((uint16_t)(p)[1] << 8))

/* Monotonic system uptime in ms (spec 18; same facility as sender_tracker.c). */
static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

typedef struct {
    int sock;
    bool ready;
    uint16_t outputIdx;
} sacn_socket_t;

static sacn_socket_t g_sacnSockets[MAX_OUTPUTS];
static int g_sacnSocketCount = 0;

esp_err_t sacn_init(void) {
    sacn_pkt_queue_init();
    g_sacnSocketCount = 0;
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        g_sacnSockets[i].sock = -1;
        g_sacnSockets[i].ready = false;
        g_sacnSockets[i].outputIdx = 0;
    }

    for (int i = 0; i < MAX_OUTPUTS; i++) {
        const DmxOutput* out = &cfg.outputs[i];
        if (!out->en) {
            continue;
        }
        int universe = out->sacn; /* 1-based sACN universe (1-63999) */
        if (universe <= 0) {
            continue;
        }

        uint8_t hi = (uint8_t)((universe >> 8) & 0xFF);
        uint8_t lo = (uint8_t)(universe & 0xFF);
        struct in_addr groupAddr;
        groupAddr.s_addr = htonl(((uint32_t)239 << 24) | ((uint32_t)255 << 16) |
                                 ((uint32_t)hi << 8) | (uint32_t)lo);

        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0) {
            LOG_ERROR("sacn", "socket create failed for output %d", i);
            continue;
        }

        int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in bind_addr;
        memset(&bind_addr, 0, sizeof(bind_addr));
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_port = htons(SACN_PORT);
        bind_addr.sin_addr = groupAddr;

        if (bind(sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
            LOG_ERROR("sacn", "bind failed for output %d (universe %d)", i, universe);
            close(sock);
            continue;
        }

        struct ip_mreq mreq;
        mreq.imr_multiaddr = groupAddr;
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            LOG_ERROR("sacn", "multicast join failed for output %d (universe %d)", i, universe);
            close(sock);
            continue;
        }

        g_sacnSockets[g_sacnSocketCount].sock = sock;
        g_sacnSockets[g_sacnSocketCount].ready = true;
        g_sacnSockets[g_sacnSocketCount].outputIdx = (uint16_t)i;
        g_sacnSocketCount++;
        LOG_INFO("sacn", "output %d: joined 239.255.%u.%u:%d (universe %d)",
                 i, hi, lo, SACN_PORT, universe);
    }

    LOG_INFO("sacn", "init complete: %d socket(s)", g_sacnSocketCount);
    return ESP_OK;
}

void sacn_poll(void) {
    uint8_t buf[SACN_MIN_SIZE];
    struct sockaddr_in src_addr;
    socklen_t src_addr_len;

    for (int i = 0; i < g_sacnSocketCount; i++) {
        sacn_socket_t* s = &g_sacnSockets[i];
        if (!s->ready || s->sock < 0) {
            continue;
        }

        for (int n = 0; n < SACN_PER_SOCKET_LIMIT; n++) {
            src_addr_len = sizeof(src_addr);
            int len = recvfrom(s->sock, buf, sizeof(buf), MSG_DONTWAIT,
                               (struct sockaddr*)&src_addr, &src_addr_len);
            if (len <= 0) {
                break; /* no data (would block) or error: stop draining this socket */
            }
            if (len < SACN_MIN_SIZE) {
                continue; /* too short: dropped silently */
            }

            uint32_t rootVec = BE32(buf + SACN_ROOT_VEC_OFFSET);
            if (rootVec != SACN_STREAM_VEC) {
                continue; /* root vector mismatch: dropped silently */
            }

            SAcnPacket pkt;
            pkt.len = (uint16_t)len;
            pkt.outputIdx = s->outputIdx;
            pkt.sourceIp = ntohl(src_addr.sin_addr.s_addr);
            memcpy(pkt.data, buf, sizeof(pkt.data));
            (void)sacn_pkt_queue_push(&pkt); /* drop on full (back-pressure) */
        }
    }

    sacn_check_timeouts();
}

/* Commit any sACN Stream-Sync staged frames whose 500 ms grace period
 * (spec 18) has elapsed since the last staging write. Runs once per poll
 * after the socket drain, so a source that never sends Sync still commits. */
void sacn_check_timeouts(void) {
    uint32_t now = now_ms();
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (!g_dmxBufState.sacnStagedValid[i]) continue;
        if (g_dmxBufState.sacnSyncDeadlineMs[i] == 0) continue;
        if ((int32_t)(now - g_dmxBufState.sacnSyncDeadlineMs[i]) >= 0) {
            commitSacnStaged(i);
        }
    }
}

bool sacn_dispatch_packet(const uint8_t* data, uint16_t len, uint32_t sourceIp) {
    if (data == NULL || len < SACN_MIN_SIZE) {
        return false;
    }

    uint32_t frameVec = BE32(data + SACN_FRAME_VEC_OFFSET);

    switch (frameVec) {
    case SACN_FRAME_VEC_STREAM: { /* 0x00000002: Streaming Data */
        uint8_t priority = data[SACN_PRIORITY_OFFSET];
        uint16_t universe = (uint16_t)(LE16(data + SACN_UNIVERSE_OFFSET) - 1); /* 1-based -> 0-based */
        uint8_t startCode = data[SACN_START_CODE_OFFSET];
        const uint8_t* payload = data + SACN_DMX_DATA_OFFSET;
        uint16_t payloadLen = (uint16_t)(len - SACN_DMX_DATA_OFFSET);
        if (payloadLen > 512) {
            payloadLen = 512; /* clamp to 512 slot bytes */
        }

        /* Stream-Sync staging (spec 18): per matching output, hold the frame
         * in the staged buffer or dispatch it immediately as before. */
        bool anyStaged = false;
        for (int i = 0; i < MAX_OUTPUTS; i++) {
            if (!cfg.outputs[i].en) continue;
            if (portAddress(&cfg.outputs[i]) != (int)universe) continue;
            if (cfg.outputs[i].sacnsync > 0) {
                uint16_t n = payloadLen;
                memcpy(g_dmxBufState.sacnStaged[i], payload, n);
                g_dmxBufState.sacnStagedLen[i] = (int)n;
                g_dmxBufState.sacnStagedValid[i] = true;
                g_dmxBufState.sacnSyncDeadlineMs[i] = now_ms() + 500;
                g_dmxBufState.sacnSyncAddr[i] = (uint16_t)cfg.outputs[i].sacnsync;
                anyStaged = true;
            }
        }
        if (!anyStaged) {
            if (startCode == 0x00) {
                routeFrame(universe, payload, payloadLen, sourceIp, PROTO_SACN, priority);
            } else {
                routeFrameNzs(universe, payload, payloadLen, startCode, sourceIp, priority);
            }
        }
        return true;
    }

    case SACN_FRAME_VEC_SYNC: { /* 0x00000003: Stream Sync */
        uint16_t syncUniverse = LE16(data + SACN_UNIVERSE_OFFSET); /* 1-based */
        for (int i = 0; i < MAX_OUTPUTS; i++) {
            if (!cfg.outputs[i].en) continue;
            if (cfg.outputs[i].sacnsync != (int)syncUniverse) continue;
            commitSacnStaged(i);
        }
        return true;
    }

    default:
        return false; /* unknown/unsupported frame vector: dropped silently */
    }
}