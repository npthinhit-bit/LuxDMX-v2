#include "ws_frame.h"

#include <string.h>

static void put_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)value;
}

static void put_i16(uint8_t *dst, int16_t value)
{
    put_u16(dst, (uint16_t)value);
}

static void put_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

size_t wsBuildFrame(uint8_t out[WS_FRAME_LEN], const ws_frame_state_t *state,
                    const ws_frame_state_t *previous)
{
    if (out == NULL || state == NULL) {
        return 0;
    }

    memset(out, 0, WS_FRAME_LEN);
    put_u16(&out[0], state->fps_x10);
    put_i16(&out[2], state->rssi);
    put_u32(&out[4], state->heap);
    put_u32(&out[8], state->uptime);
    out[12] = state->senders;
    out[13] = state->source_status;
    put_u16(&out[14], state->jitter_x10);

    uint8_t changed = 0;
    for (size_t output = 0; output < WS_OUTPUT_COUNT; ++output) {
        const size_t offset = WS_DMX_DATA_OFFSET + output * 512u;
        memcpy(&out[offset], state->dmx[output], 512u);
        if (previous == NULL || memcmp(state->dmx[output], previous->dmx[output], 512u) != 0) {
            changed |= (uint8_t)(1u << output);
        }
    }

    for (size_t output = 0; output < WS_OUTPUT_COUNT; ++output) {
        put_u16(&out[WS_STATS_OFFSET + output * 2u], state->output_fps_x10[output]);
        put_u16(&out[WS_STATS_OFFSET + 8u + output * 2u], state->input_fps_x10[output]);
        out[WS_STATS_OFFSET + 16u + output] = state->tx_style[output];
    }
    out[WS_CHANGED_OFFSET] = changed;
    put_u16(&out[WS_RDM_TAIL_OFFSET], state->rdm_count);
    put_u32(&out[WS_RDM_TAIL_OFFSET + 2u], state->rdm_sent);
    put_u32(&out[WS_RDM_TAIL_OFFSET + 6u], state->rdm_received);
    return WS_FRAME_LEN;
}

bool wsShouldPush(uint8_t changed_universes, uint8_t subscription_mask,
                  uint32_t frame_sequence, uint32_t client_sequence)
{
    if (frame_sequence != client_sequence) {
        return (changed_universes & subscription_mask) != 0;
    }
    return false;
}
