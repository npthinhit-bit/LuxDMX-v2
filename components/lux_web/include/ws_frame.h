#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WS_MAX_CLIENTS 12u
#define WS_OUTPUT_COUNT 4u
#define WS_FRAME_LEN 2095u
#define WS_DMX_DATA_OFFSET 16u
#define WS_STATS_OFFSET 2064u
#define WS_CHANGED_OFFSET 2084u
#define WS_RDM_TAIL_OFFSET 2085u

typedef struct {
    uint16_t fps_x10;
    int16_t rssi;
    uint32_t heap;
    uint32_t uptime;
    uint8_t senders;
    uint8_t source_status;
    uint16_t jitter_x10;
    uint8_t dmx[WS_OUTPUT_COUNT][512];
    uint16_t output_fps_x10[WS_OUTPUT_COUNT];
    uint16_t input_fps_x10[WS_OUTPUT_COUNT];
    uint8_t tx_style[WS_OUTPUT_COUNT];
    uint16_t rdm_count;
    uint32_t rdm_sent;
    uint32_t rdm_received;
} ws_frame_state_t;

/* Build the fixed-size big-endian binary frame. `previous` may be NULL. */
size_t wsBuildFrame(uint8_t out[WS_FRAME_LEN], const ws_frame_state_t *state,
                    const ws_frame_state_t *previous);

/* Delta subscription predicate used by the 12-client push loop. */
bool wsShouldPush(uint8_t changed_universes, uint8_t subscription_mask,
                 uint32_t frame_sequence, uint32_t client_sequence);

#ifdef __cplusplus
}
#endif
