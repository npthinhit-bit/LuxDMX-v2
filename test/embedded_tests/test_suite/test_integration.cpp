// Master test runner for the embedded Unity suite on ESP32-S3.
// Provides the ONLY setUp()/tearDown()/setup()/loop() — all other test
// TUs forward-declare their test functions here and register them in setup().
#include "config_core.h"
#include "config_types.h"
#include "rdm_engine.h"
#include "rdm_types.h"
#include "Preferences.h"

#include <string.h>
#include <unity.h>

// --- Forward declarations for tests in other TUs -------------------------------
// test_rdm_transport.cpp
void test_rdm_build_layout(void);
void test_rdm_build_checksum(void);
void test_rdm_build_transaction_increments(void);
void test_rdm_build_no_param_data(void);
void test_put_uid_big_endian(void);

// test_rdm_disc.cpp
void test_uid_pack_roundtrip(void);
void test_uid_pack_order(void);
void test_uid_pack_broadcast(void);
void test_uid_pack_max(void);

// test_dmx_rmt.cpp
void test_rmt_dmx_encode_break_mab(void);
void test_rmt_dmx_encode_byte_0x00(void);
void test_rmt_dmx_encode_byte_0xff(void);
void test_rmt_dmx_encode_invert(void);
void test_rmt_dmx_encode_length(void);

// test_gpio_dir.cpp
void test_gpio_set_high(void);
void test_gpio_set_low(void);
void test_gpio_init_output(void);

// test_config.cpp
void test_config_template_resolution(void);
void test_config_set_get_int(void);
void test_config_set_get_bool(void);
void test_config_set_get_string(void);
void test_config_set_unknown_key(void);
void test_config_save_load_roundtrip(void);

// --- Master setUp / tearDown ----------------------------------------------------
void setUp(void)
{
    // Reset RDM controller state so every test starts from a known baseline.
    // rdmBuild increments g_rdm.tn; without a reset, test_rdm_build_layout
    // (which expects tn==0) would fail when run after other rdmBuild tests.
    g_rdm = RdmState{};

    // Clear the NVS config namespace so cfg save/load tests are deterministic.
    Preferences prefs;
    prefs.begin("dmxgw", false);
    prefs.clear();
    prefs.end();
}

void tearDown(void)
{
}

// --- Integration tests ----------------------------------------------------------

// Verify the global RdmState is default-initialized with the LuxDMX controller UID.
void test_rdm_state_defaults(void)
{
    // Ctrl UID: manufacturer 0x4C58 ("LX"), device ID 0 (unset before rdmInitCtrlUid).
    TEST_ASSERT_EQUAL_UINT16(0x4C58, g_rdm.ctrl.man_id);
    TEST_ASSERT_EQUAL_UINT32(0, g_rdm.ctrl.dev_id);

    // Transaction number starts at 0 (wraps at 255).
    TEST_ASSERT_EQUAL_UINT8(0, g_rdm.tn);

    // No RDM lines registered until outputInitAll() calls rdmRmtInit().
    TEST_ASSERT_EQUAL_INT(0, g_rdm.lineN);

    // No sender lines active.
    TEST_ASSERT_NULL(g_rdm.rmt);
}

// Verify the config engine and template system produce a consistent state
// after resetToTemplate() — global + per-output fields from _base + esp32s3dev.
void test_config_load_after_init(void)
{
    cfgcore::resetToTemplate();

    // Global fields from _base
    TEST_ASSERT_EQUAL_STRING("dmx-gateway", cfg.hostname.c_str());
    TEST_ASSERT_EQUAL_INT(2, cfg.protocol);

    // esp32s3dev overrides
    TEST_ASSERT_EQUAL_INT(48, cfg.ledPin);
    TEST_ASSERT_EQUAL_INT(2, cfg.ledType);

    // Per-output fields from _base (a_en=1, a_tx=17, a_rx=16)
    TEST_ASSERT_TRUE(cfg.outputs[0].enabled);
    TEST_ASSERT_EQUAL_INT(17, cfg.outputs[0].txPin);
    TEST_ASSERT_EQUAL_INT(16, cfg.outputs[0].rxPin);

    // Per-output fields from _base (b_uni=1, b_port=2)
    TEST_ASSERT_EQUAL_INT(1, cfg.outputs[1].universe);
    TEST_ASSERT_EQUAL_INT(2, cfg.outputs[1].port);

    // Break/MAB defaults from _base (a_brk=176, a_mab=12)
    TEST_ASSERT_EQUAL_INT(176, cfg.outputs[0].breakTime);
    TEST_ASSERT_EQUAL_INT(12, cfg.outputs[0].mabTime);
}

// --- Test runner ----------------------------------------------------------------

void setup()
{
    UNITY_BEGIN();

    // RDM transport framing
    RUN_TEST(test_rdm_build_layout);
    RUN_TEST(test_rdm_build_checksum);
    RUN_TEST(test_rdm_build_transaction_increments);
    RUN_TEST(test_rdm_build_no_param_data);
    RUN_TEST(test_put_uid_big_endian);

    // RDM UID pack/unpack
    RUN_TEST(test_uid_pack_roundtrip);
    RUN_TEST(test_uid_pack_order);
    RUN_TEST(test_uid_pack_broadcast);
    RUN_TEST(test_uid_pack_max);

    // DMX RMT encoding
    RUN_TEST(test_rmt_dmx_encode_break_mab);
    RUN_TEST(test_rmt_dmx_encode_byte_0x00);
    RUN_TEST(test_rmt_dmx_encode_byte_0xff);
    RUN_TEST(test_rmt_dmx_encode_invert);
    RUN_TEST(test_rmt_dmx_encode_length);

    // GPIO direction control
    RUN_TEST(test_gpio_set_high);
    RUN_TEST(test_gpio_set_low);
    RUN_TEST(test_gpio_init_output);

    // Config engine
    RUN_TEST(test_config_template_resolution);
    RUN_TEST(test_config_set_get_int);
    RUN_TEST(test_config_set_get_bool);
    RUN_TEST(test_config_set_get_string);
    RUN_TEST(test_config_set_unknown_key);
    RUN_TEST(test_config_save_load_roundtrip);

    // Integration tests
    RUN_TEST(test_rdm_state_defaults);
    RUN_TEST(test_config_load_after_init);

    UNITY_END();
}

void loop()
{
}
