// Unity tests for config_core.h — template resolution, setValue/getValue
// round-trip, and NVS save/load persistence.
#include "Preferences.h"
#include "config_core.h"
#include <unity.h>

// NVS namespace used by cfgcore (must match CFG_PREF_NS in config_core.cpp).
#define CFG_PREF_NS "dmxgw"

void test_config_template_resolution(void)
{
    cfgcore::resetToTemplate();

    // _base template sets hostname=dmx-gateway and protocol=2.
    TEST_ASSERT_EQUAL_STRING("dmx-gateway", cfg.hostname.c_str());
    TEST_ASSERT_EQUAL_INT(2, cfg.protocol);

    // esp32s3dev template overrides: ledpin=48, ledtype=2 (WS2812 on GPIO48).
    TEST_ASSERT_EQUAL_INT(48, cfg.ledPin);
    TEST_ASSERT_EQUAL_INT(2, cfg.ledType);

    // Output A is enabled by default in _base with tx=17, rx=16.
    TEST_ASSERT_TRUE(cfg.outputs[0].enabled);
    TEST_ASSERT_EQUAL_INT(17, cfg.outputs[0].txPin);
    TEST_ASSERT_EQUAL_INT(16, cfg.outputs[0].rxPin);
}

void test_config_set_get_int(void)
{
    cfgcore::resetToTemplate();

    String err;
    TEST_ASSERT_EQUAL_INT(ESP_OK, cfgcore::setValue("ledpin", "21", err));

    String out;
    TEST_ASSERT_EQUAL_INT(ESP_OK, cfgcore::getValue("ledpin", out));
    TEST_ASSERT_EQUAL_INT(21, out.toInt());
    TEST_ASSERT_EQUAL_INT(21, cfg.ledPin);
}

void test_config_set_get_bool(void)
{
    cfgcore::resetToTemplate();

    String err;
    TEST_ASSERT_EQUAL_INT(ESP_OK, cfgcore::setValue("artrdm", "0", err));

    String out;
    TEST_ASSERT_EQUAL_INT(ESP_OK, cfgcore::getValue("artrdm", out));
    TEST_ASSERT_EQUAL_STRING("false", out.c_str());
    TEST_ASSERT_FALSE(cfg.artnetRdm);
}

void test_config_set_get_string(void)
{
    cfgcore::resetToTemplate();

    String err;
    TEST_ASSERT_EQUAL_INT(ESP_OK, cfgcore::setValue("hostname", "testbox", err));

    String out;
    TEST_ASSERT_EQUAL_INT(ESP_OK, cfgcore::getValue("hostname", out));
    TEST_ASSERT_EQUAL_STRING("testbox", out.c_str());
    TEST_ASSERT_EQUAL_STRING("testbox", cfg.hostname.c_str());
}

void test_config_set_unknown_key(void)
{
    cfgcore::resetToTemplate();

    String err;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, cfgcore::setValue("nonexistent", "42", err));
    TEST_ASSERT_TRUE(err.length() > 0);
}

void test_config_save_load_roundtrip(void)
{
    cfgcore::resetToTemplate();

    String err;
    cfgcore::setValue("hostname", "persist-test", err);
    cfgcore::setValue("ledpin", "33", err);

    cfgcore::save();

    // Reset in-memory state, then load from NVS — saved values should persist.
    cfgcore::resetToTemplate();
    TEST_ASSERT_EQUAL_STRING("dmx-gateway", cfg.hostname.c_str());
    TEST_ASSERT_EQUAL_INT(48, cfg.ledPin);

    cfgcore::load();
    TEST_ASSERT_EQUAL_STRING("persist-test", cfg.hostname.c_str());

    String out;
    cfgcore::getValue("ledpin", out);
    TEST_ASSERT_EQUAL_INT(33, out.toInt());
}
