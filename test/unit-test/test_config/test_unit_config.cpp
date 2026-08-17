// Unity unit tests for the config engine (cfgcore::, cfgserial::).
#include "Preferences.h"
#include "config_core.h"
#include "config_serial.h"
#include <unity.h>

void setUp(void)
{
    Preferences::clearAll();
    cfgcore::resetToTemplate();
}
void tearDown(void) {}

void test_config_defaults_from_template(void)
{
    TEST_ASSERT_EQUAL_STRING("dmx-gateway", cfg.hostname.c_str());
    TEST_ASSERT_EQUAL_INT(2, cfg.protocol);
    TEST_ASSERT_EQUAL_INT(1, cfg.ledType);
}

void test_config_set_and_get(void)
{
    String err;
    TEST_ASSERT_EQUAL_INT(ESP_OK, cfgcore::setValue("hostname", "testbox", err));
    TEST_ASSERT_EQUAL_STRING("testbox", cfg.hostname.c_str());
    String out;
    TEST_ASSERT_EQUAL_INT(ESP_OK, cfgcore::getValue("hostname", out));
    TEST_ASSERT_EQUAL_STRING("testbox", out.c_str());
}

void test_config_nvs_roundtrip(void)
{
    String err;
    TEST_ASSERT_EQUAL_INT(ESP_OK, cfgcore::setValue("hostname", "gateway1", err));
    cfgcore::save();
    cfgcore::resetToTemplate();
    TEST_ASSERT_EQUAL_STRING("dmx-gateway", cfg.hostname.c_str());
    cfgcore::load();
    TEST_ASSERT_EQUAL_STRING("gateway1", cfg.hostname.c_str());
}

void test_config_template_luxdmx_4uni(void)
{
    String err;
    TEST_ASSERT_EQUAL_INT(ESP_OK, cfgcore::resetTo("luxdmx_4uni", err));
    TEST_ASSERT_EQUAL_INT(3, cfg.ledType);
    TEST_ASSERT_TRUE(cfg.outputs[0].enabled);
    TEST_ASSERT_TRUE(cfg.outputs[0].rtsPin >= 0);
    TEST_ASSERT_EQUAL_INT(1, cfg.outputs[0].mode);
    TEST_ASSERT_TRUE(cfg.outputs[2].enabled);
    TEST_ASSERT_TRUE(cfg.outputs[2].rtsPin < 0);
    TEST_ASSERT_EQUAL_INT(0, cfg.outputs[2].mode);
}

void test_config_serial_set_command(void)
{
    String err;
    TEST_ASSERT_EQUAL_INT(ESP_OK, cfgcore::setValue("hostname", "mydevice", err));
    String r = cfgserial::execute("set ledtype 3");
    TEST_ASSERT_EQUAL_STRING("OK", r.c_str());
    TEST_ASSERT_EQUAL_INT(3, cfg.ledType);
}

void test_config_serial_get_command(void)
{
    String err;
    TEST_ASSERT_EQUAL_INT(ESP_OK, cfgcore::setValue("hostname", "mydevice", err));
    String r = cfgserial::execute("get hostname");
    TEST_ASSERT_EQUAL_STRING("hostname=mydevice", r.c_str());
}

void test_config_serial_dump(void)
{
    String err;
    TEST_ASSERT_EQUAL_INT(ESP_OK, cfgcore::setValue("hostname", "mydevice", err));
    String r = cfgserial::execute("dump");
    TEST_ASSERT_TRUE(r.indexOf("hostname=mydevice") >= 0);
}

void test_config_serial_invalid_command(void)
{
    String r = cfgserial::execute("bogus");
    TEST_ASSERT_TRUE(r.indexOf("ERR") >= 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_config_defaults_from_template);
    RUN_TEST(test_config_set_and_get);
    RUN_TEST(test_config_nvs_roundtrip);
    RUN_TEST(test_config_template_luxdmx_4uni);
    RUN_TEST(test_config_serial_set_command);
    RUN_TEST(test_config_serial_get_command);
    RUN_TEST(test_config_serial_dump);
    RUN_TEST(test_config_serial_invalid_command);
    return UNITY_END();
}
