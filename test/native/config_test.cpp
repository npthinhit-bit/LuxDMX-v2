// Minimal smoke test for the v2 config engine.
#include "Preferences.h"
#include "config_core.h"
#include "config_serial.h"
#include <cstdio>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg)                 \
    do                                   \
    {                                    \
        if (cond)                        \
            g_pass++;                    \
        else                             \
        {                                \
            g_fail++;                    \
            printf("  FAIL: %s\n", msg); \
        }                                \
    } while (0)

int main()
{
    // 1. Template resolution: neutral -> active template -> (no NVS) -> check a few values
    cfgcore::resetToTemplate();
    CHECK(cfg.hostname == "dmx-gateway", "hostname matches _base");
    CHECK(cfg.protocol == 2, "protocol = Art-Net+sACN");

    // 2. luxdmx_4uni template: outputs A+B RDM-capable, C+D DMX-only
    {
        String err;
        CHECK(cfgcore::resetTo("luxdmx_4uni", err) == ESP_OK, "resetTo luxdmx_4uni");
        CHECK(cfg.outputs[0].enabled && cfg.outputs[0].rtsPin >= 0 && cfg.outputs[0].mode == 1, "out A RDM full");
        CHECK(cfg.outputs[1].enabled && cfg.outputs[1].rtsPin >= 0 && cfg.outputs[1].mode == 1, "out B RDM full");
        CHECK(cfg.outputs[2].enabled && cfg.outputs[2].rtsPin < 0 && cfg.outputs[2].mode == 0, "out C DMX only");
        CHECK(cfg.outputs[3].enabled && cfg.outputs[3].rtsPin < 0 && cfg.outputs[3].mode == 0, "out D DMX only");
        CHECK(cfg.ledType == 3, "ledType = 5-LED panel");
        CHECK(cfg.useEthernet == false, "useEthernet = false by default");
    }

    // 3. setValue / getValue round-trip
    {
        String err;
        CHECK(cfgcore::setValue("hostname", "testbox", err) == ESP_OK, "setValue hostname");
        CHECK(cfg.hostname == "testbox", "hostname round-trip");
        String out;
        CHECK(cfgcore::getValue("hostname", out) == ESP_OK, "getValue hostname");
        CHECK(out == "testbox", "getValue returns testbox");
    }

    // 4. save / load round-trip
    {
        cfgcore::save();
        cfgcore::resetToTemplate();
        CHECK(cfg.hostname == "dmx-gateway", "load from template after reset");
        cfgcore::load();
        CHECK(cfg.hostname == "testbox", "load restores saved hostname");
    }

    // 5. Serial console grammar
    {
        cfgserial::Hooks hooks;
        String           r = cfgserial::execute("dump");
        CHECK(r.indexOf("hostname=testbox") >= 0, "dump shows hostname");
        CHECK(cfgserial::execute("get hostname") == "hostname=testbox", "get hostname");
        CHECK(cfgserial::execute("set ledtype 3") == "OK", "set ledtype");
        CHECK(cfg.ledType == 3, "ledtype applied");
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
