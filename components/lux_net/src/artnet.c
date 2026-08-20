/*
 * spec 17 - Art-Net Protocol Subsystem
 *
 * Inbound UDP reception on port 6454 (SO_REUSEADDR + SO_BROADCAST, non-blocking)
 * and opcode dispatch. ArtDMX is routed to the frame router with its priority
 * byte (byte 59, default 100) and DMX payload (byte 60+); ArtSync commits
 * staged DMX buffers; ArtPoll is stubbed pending bridge integration; ArtNzs
 * is routed via the non-zero-start-code router with the start code at byte 60.
 */
#include "artnet.h"
#include "logger.h"
#include "frame_router.h"
#include "dmx_buffer.h"
#include "lwip/sockets.h"
#include "lwip/ip_addr.h"
#include <string.h>
#include <fcntl.h>

ArtNetState g_artnet = { .sock = -1, .ready = false };

esp_err_t artnet_init(void) {
    g_artnet.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (g_artnet.sock < 0) {
        LOG_ERROR("artnet", "UDP socket creation failed");
        g_artnet.ready = false;
        return ESP_FAIL;
    }

    int opt = 1;
    setsockopt(g_artnet.sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(g_artnet.sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));

    int flags = fcntl(g_artnet.sock, F_GETFL, 0);
    if (flags != -1) {
        fcntl(g_artnet.sock, F_SETFL, flags | O_NONBLOCK);
    }

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(ARTNET_PORT),
        .sin_addr.s_addr = INADDR_ANY
    };

    if (bind(g_artnet.sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        LOG_ERROR("artnet", "bind to port %d failed", ARTNET_PORT);
        close(g_artnet.sock);
        g_artnet.sock = -1;
        g_artnet.ready = false;
        return ESP_FAIL;
    }

    g_artnet.ready = true;
    LOG_INFO("artnet", "listening on UDP port %d", ARTNET_PORT);
    return ESP_OK;
}

void artnet_poll(void) {
    if (!g_artnet.ready || g_artnet.sock < 0) {
        return;
    }

    uint8_t buf[ARTNET_MAX_PACKET];
    struct sockaddr_in src_addr;
    socklen_t src_addr_len;

    for (int i = 0; i < MAX_PACKETS_PER_TICK; i++) {
        src_addr_len = sizeof(src_addr);

        int len = recvfrom(g_artnet.sock, buf, sizeof(buf), 0,
                           (struct sockaddr*)&src_addr, &src_addr_len);
        if (len <= 0) {
            break;
        }
        if (len < ARTNET_MIN_PACKET) {
            continue;
        }
        if (memcmp(buf, ARTNET_ID, 8) != 0) {
            continue;
        }

        uint32_t sourceIp = ntohl(src_addr.sin_addr.s_addr);
        (void)artnet_dispatch_packet(buf, (uint16_t)len, sourceIp);
    }
}

bool artnet_dispatch_packet(const uint8_t* data, uint16_t len, uint32_t sourceIp) {
    if (data == NULL || len < 10) {
        return false;
    }

    uint16_t opcode = (uint16_t)(data[8] | (data[9] << 8));

    switch (opcode) {
    case 0x0050: {                                  /* ArtDMX */
        if (len < 18) {
            return false;
        }
        uint16_t universe = (uint16_t)(data[14] | (data[15] << 8));
        uint16_t length = (uint16_t)((data[16] << 8) | data[17]);
        uint8_t priority = (len >= 60) ? data[59] : 100;
        uint16_t avail = (len >= 60) ? (uint16_t)(len - 60) : 0;
        if (length > avail) {
            length = avail;
        }
        const uint8_t* payload = (avail > 0) ? data + 60 : NULL;
        routeFrame(universe, payload, length, sourceIp, 0, priority);
        return true;
    }

    case 0x0053: {                                  /* ArtSync */
        flushArtSyncStaged();
        return true;
    }

    case 0x0200: {                                  /* ArtPoll (forward to bridge) */
        LOG_DEBUG("artnet", "ArtPoll received (bridge not yet implemented)");
        return false;
    }

    case 0x0058: {                                  /* ArtNzs */
        if (len < 61) {
            return false;
        }
        uint16_t universe = (uint16_t)(data[14] | (data[15] << 8));
        uint16_t length = (uint16_t)((data[16] << 8) | data[17]);
        uint8_t startCode = data[60];
        uint8_t priority = data[59];
        uint16_t avail = (uint16_t)(len - 61);
        if (length > avail) {
            length = avail;
        }
        const uint8_t* payload = (avail > 0) ? data + 61 : NULL;
        routeFrameNzs(universe, payload, length, startCode, sourceIp, priority);
        return true;
    }

    default:
        return false;
    }
}