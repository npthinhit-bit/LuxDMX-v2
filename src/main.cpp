#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "config_schema.h"
#include "config_enums.h"
#include "config_core.h"
#include "config_serial.h"
#include "nvs_migrate.h"
#include "output_init.h"
#include "input_router.h"
#include "scene_engine.h"
#include "rdm_engine.h"
#include "net/sacn.h"
#include "net/artnet.h"
#include "net/ws_frame.h"
#include "net/websocket.h"
#include "net/ws_handler.h"
#include "net/web_server.h"
#include "net/net_state.h"
#include "net/ethernet.h"
#include "stats.h"
#include "sys_platform.h"
#include "tasks.h"
#include "led_status.h"
#include "display.h"
#include "firmware_version.h"
#include "net/ota.h"
#include "sys/soak_monitor.h"
#include "sys/syslog.h"

// Thin wiring entry point — setup() + loop() delegate to all modules.
// See .kilo/plans/...modular-arch-plan.md for the dependency graph and
// data-flow design (issue #64 core separation, seqlock buffer, etc.).

static const char* PREF_NS_PTR = "dmxgw";

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000);   // wait for serial monitor (debug builds)

    startMs = millis();

    // 1. NVS migration + config load
    nvs_migrate::migrateNvsKeys(PREF_NS_PTR);
    cfgcore::load();
    sanitizeOutputs();

    // 2. Serial config console
    cfgserial::begin(Serial, {});

    // 3. LED + display
    initLed();
    setLedColor(0x0a0a0a, true);   // white = booting
    initDisplay();

    // 4. Network bring-up (interface -> mode, with AP fallback)
#if defined(HAS_WIRED_ETH)
    g_useEth = cfg.useEthernet;
#if defined(HAS_ETH_SPI)
    if (cfg.wiredPhy == WIRED_PHY_SPI) g_useEth = g_useEth && cfg.ethW5500;
#endif
    if (g_useEth) {
        startWiredEth();
        if (!netConnected()) {
            Serial.printf("[NET] wired link-loss policy %d\n", cfg.linkLossMode);
            applyWiredLinkLoss(true);
        }
    }
#endif
    if (cfg.wifiMode == NET_WIFI_AP) {
        startWiFiAP();
    } else {
        startWiFiStation();
    }

    // 5. OTA boot update + mDNS
    otaBootUpdate();
    if (MDNS.begin(cfg.hostname.c_str())) {
        MDNS.addService("http",   "tcp", 80);
        if (cfg.protocol != 1) MDNS.addService("artnet", "udp", 6454);
        if (cfg.protocol != 0) MDNS.addService("e131",   "udp", 5568);
    }

    // 6. Output init (RMT channels, UART RX, DE/RE GPIO, RDM lines)
    dmxInitGuardBegin();
    outputInitAll();
    dmxInitGuardEnd();
    sceneLoadAll();
    syslogInit();
    initOTA();
    soakInit();

    // 7. Network protocol init
    if (cfg.protocol != 1) {
        Serial.printf("[ArtNet] out0 universe %d\n", cfg.outputs[0].universe);
    }
    artRdmInit();
    if (cfg.protocol != 0) startSacn();

    // 8. Web server + WebSocket
    webRegisterRoutes();
    wsInit(http);
    http.begin();

    // 9. Spawn tasks
    createTasks();
}

void loop() {
    // Serial config console (non-blocking line reader)
    cfgserial::poll();

    // Setup portal captive DNS pump
    if (g_setupPortal) dnsServer.processNextRequest();

    // Dirty-flag persistence
    if (rdmPollDirty) { rdmPollDirty = false; rdmSavePoll(); }
    if (g_artCfgDirty) { g_artCfgDirty = false; saveConfig(); }
    if (g_bqDirty) {
        g_bqDirty = false;
        Preferences p; p.begin(PREF_NS, false);
        p.putUChar("bqpolicy", g_bqPolicy); p.end();
    }

    // Process queued WebSocket RDM operations (runs on core 0, outside RMT use)
    rdmWsProcessQueued();

    // DMX input polling (converter mode: DMX-in -> Art-Net/sACN retransmit)
    inputRouterPoll();

    // WebSocket live push (~10 Hz) + meta (~2 Hz)
    uint32_t now = millis();
    static uint32_t lastWsPush = 0, lastMetaPush = 0;
    if (now - lastWsPush >= 100) {
        wsPush();
        lastWsPush = now;
    }
    if (now - lastMetaPush >= 2000) {
        wsPushMeta();
        lastMetaPush = now;
    }
}
