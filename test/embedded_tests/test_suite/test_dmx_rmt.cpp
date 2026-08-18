// Unity tests for rmtDmxEncode (dmx_rmt.h) — symbol buffer encoding only.
// No RMT hardware calls; verifies the static rmtDmxEncode() function that
// packs break/MAB + data bytes into the RMT symbol word buffer.
#include "dmx_rmt.h"
#include <string.h>
#include <unity.h>

static rmt_symbol_word_t test_sym[RMT_DMX_MAX_SYM];

void test_rmt_dmx_encode_break_mab(void)
{
    RmtDmx rd = {};
    rd.sym = test_sym;
    memset(test_sym, 0xFF, sizeof(test_sym));

    const uint8_t data[1] = {0x00};
    rmtDmxEncode(&rd, data, 1);

    // First symbol word: break=low(176) + MAB=high(12)
    TEST_ASSERT_EQUAL_INT(RMT_DMX_BREAK_DEFAULT, rd.sym[0].duration0);
    TEST_ASSERT_EQUAL_INT(0, rd.sym[0].level0);   // break = low
    TEST_ASSERT_EQUAL_INT(RMT_DMX_MAB_DEFAULT, rd.sym[0].duration1);
    TEST_ASSERT_EQUAL_INT(1, rd.sym[0].level1);   // MAB = high
}

void test_rmt_dmx_encode_byte_0x00(void)
{
    RmtDmx rd = {};
    rd.sym = test_sym;
    memset(test_sym, 0, sizeof(test_sym));

    const uint8_t data[1] = {0x00};
    rmtDmxEncode(&rd, data, 1);

    // Byte 0x00: start bit(0) + 8 data(0) + 2 stop(1)
    // Runs: 0(9 × 4us = 36) + 1(2 × 4us = 8) → one symbol word
    // sym[1] is the data byte word (sym[0] is break/MAB)
    TEST_ASSERT_EQUAL_INT(36, rd.sym[1].duration0);
    TEST_ASSERT_EQUAL_INT(0, rd.sym[1].level0);
    TEST_ASSERT_EQUAL_INT(8, rd.sym[1].duration1);
    TEST_ASSERT_EQUAL_INT(1, rd.sym[1].level1);
}

void test_rmt_dmx_encode_byte_0xff(void)
{
    RmtDmx rd = {};
    rd.sym = test_sym;
    memset(test_sym, 0, sizeof(test_sym));

    const uint8_t data[1] = {0xFF};
    rmtDmxEncode(&rd, data, 1);

    // Byte 0xFF: start bit(0) + 8 data(1) + 2 stop(1)
    // Runs: 0(1 × 4us = 4) + 1(10 × 4us = 40) → one symbol word
    TEST_ASSERT_EQUAL_INT(4, rd.sym[1].duration0);
    TEST_ASSERT_EQUAL_INT(0, rd.sym[1].level0);
    TEST_ASSERT_EQUAL_INT(40, rd.sym[1].duration1);
    TEST_ASSERT_EQUAL_INT(1, rd.sym[1].level1);
}

void test_rmt_dmx_encode_invert(void)
{
    RmtDmx rd = {};
    rd.sym = test_sym;
    rd.invert = true;
    memset(test_sym, 0, sizeof(test_sym));

    const uint8_t data[1] = {0x00};
    rmtDmxEncode(&rd, data, 1);

    // Inverted break word: MAB(12, high) + break(176, low)
    TEST_ASSERT_EQUAL_INT(RMT_DMX_MAB_DEFAULT, rd.sym[0].duration0);
    TEST_ASSERT_EQUAL_INT(1, rd.sym[0].level0);   // MAB inverted = high
    TEST_ASSERT_EQUAL_INT(RMT_DMX_BREAK_DEFAULT, rd.sym[0].duration1);
    TEST_ASSERT_EQUAL_INT(0, rd.sym[0].level1);   // break inverted = low

    // Inverted data byte: levels swapped (0→1, 1→0) but durations preserved
    TEST_ASSERT_EQUAL_INT(36, rd.sym[1].duration0);
    TEST_ASSERT_EQUAL_INT(1, rd.sym[1].level0);      // was 0, swapped to 1
    TEST_ASSERT_EQUAL_INT(8, rd.sym[1].duration1);
    TEST_ASSERT_EQUAL_INT(0, rd.sym[1].level1);      // was 1, swapped to 0
}

void test_rmt_dmx_encode_length(void)
{
    RmtDmx rd = {};
    rd.sym = test_sym;
    memset(test_sym, 0, sizeof(test_sym));

    // 3-byte frame: [0x00, 0xFF, 0x00] — each byte is 1 word, plus break word
    const uint8_t data[3] = {0x00, 0xFF, 0x00};
    int           nsym = rmtDmxEncode(&rd, data, 3);

    TEST_ASSERT_EQUAL_INT(4, nsym);       // 1 (break) + 3 (data bytes)
    TEST_ASSERT_EQUAL_INT(4, rd.nsym);
}
