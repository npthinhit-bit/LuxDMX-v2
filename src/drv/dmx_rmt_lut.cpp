#include "dmx_rmt.h"

rmt_symbol_word_t g_byteLut[256][RMT_MAX_WORDS_PER_BYTE];
uint8_t           g_byteLutN[256];
rmt_symbol_word_t g_byteLutInv[256][RMT_MAX_WORDS_PER_BYTE];
uint8_t           g_byteLutInvN[256];
bool              g_lutReady = false;
