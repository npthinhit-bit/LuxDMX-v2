#include "ethernet.h"
#include "eth_phy.h"
#include "config_schema.h"
#include "network.h"     // startWiFiAP, startWiFiStation, g_* flags
#include "led_status.h"
#include <esp_rom_sys.h>

static volatile bool s_ethUpDone;
static volatile bool s_ethRmiiUpDone;

void waitEthLink() {
    uint32_t t0 = millis();
    while (!ETH.linkUp() && millis() - t0 < 15000) delay(50);
    if (ETH.linkUp())
        Serial.printf("[ETH] %s\n", netLocalIP().toString().c_str());
    else
        Serial.println("[ETH] link up timeout");
}

void applyEthStaticIp() {
    if (!cfg.staticIp) { ETH.config((uint32_t)0, (uint32_t)0, (uint32_t)0); return; }
    IPAddress ip, gw, sn, dns;
    parseIp(cfg.ip, ip); parseIp(cfg.gateway, gw);
    parseIp(cfg.subnet, sn); parseIp(cfg.dns, dns);
    ETH.config(ip, gw, sn, dns);
}

static void w5500HardReset() {
    if (cfg.ethRst < 0) return;
    pinMode(cfg.ethRst, OUTPUT);
    digitalWrite(cfg.ethRst, LOW);
    delayMicroseconds(600);
    digitalWrite(cfg.ethRst, HIGH);
    delay(2);
    Serial.printf("[ETH] W5500 hard reset on GPIO%d\n", cfg.ethRst);
}

#if defined(HAS_ETH_SPI)
static void ethSpiUpTask(void*) {
    eth_phy_type_t phy = ETH_PHY_W5500;
#if defined(CONFIG_ETH_SPI_ETHERNET_DM9051)
    if (cfg.ethSpiPhy == ETH_SPI_PHY_DM9051) phy = ETH_PHY_DM9051;
#else
    if (cfg.ethSpiPhy == ETH_SPI_PHY_DM9051)
        Serial.println("[ETH] DM9051 not in build, using W5500");
#endif
    bool isW5500 = (phy == ETH_PHY_W5500);
    if (isW5500) w5500HardReset();
    ETH.begin(phy, ETH_W5500_ADDR, cfg.ethCs, cfg.ethInt,
              isW5500 ? -1 : cfg.ethRst,
              ETH_W5500_SPI_HOST, cfg.ethSck, cfg.ethMiso, cfg.ethMosi,
              cfg.ethFreqMhz);
    ETH.setHostname(cfg.hostname.c_str());
    applyEthStaticIp();
    waitEthLink();
    s_ethUpDone = true;
    vTaskDelete(NULL);
}

static void startEthSpi() {
    Serial.printf("[ETH] %s SPI cs=%d irq=%d rst=%d sck=%d miso=%d mosi=%d freq=%dMHz\n",
        cfg.ethSpiPhy == ETH_SPI_PHY_DM9051 ? "DM9051" : "W5500",
        cfg.ethCs, cfg.ethInt, cfg.ethRst, cfg.ethSck, cfg.ethMiso, cfg.ethMosi, cfg.ethFreqMhz);
    s_ethUpDone = false;
    xTaskCreatePinnedToCore(ethSpiUpTask, "ethup", 8192, nullptr, 5, nullptr, 0);
    uint32_t t0 = millis();
    while (!s_ethUpDone && millis() - t0 < 30000) delay(20);
    if (!s_ethUpDone) Serial.println("[ETH] SPI bring-up still running after 30s");
}
#endif // HAS_ETH_SPI

#if defined(HAS_ETH_RMII)
static eth_clock_mode_t rmiiClkMode(int idx) {
    switch (idx) {
        case 1: return ETH_CLOCK_GPIO0_OUT;
        case 2: return ETH_CLOCK_GPIO16_OUT;
        case 3: return ETH_CLOCK_GPIO17_OUT;
        default: return ETH_CLOCK_GPIO0_IN;
    }
}
static void ethRmiiUpTask(void*) {
    int phy = constrain(cfg.rmiiPhy, 0, RMII_PHY_COUNT - 1);
    ETH.begin(rmiiPhyType(phy), cfg.rmiiAddr, cfg.rmiiMdc, cfg.rmiiMdio,
              cfg.rmiiPwr, rmiiClkMode(cfg.rmiiClk));
    ETH.setHostname(cfg.hostname.c_str());
    applyEthStaticIp();
    waitEthLink();
    s_ethRmiiUpDone = true;
    vTaskDelete(NULL);
}
static void startEthRmii() {
    int phy = constrain(cfg.rmiiPhy, 0, RMII_PHY_COUNT - 1);
    Serial.printf("[ETH] %s RMII addr=%d mdc=%d mdio=%d pwr=%d clk=%d\n",
        RMII_PHY_NAMES[phy], cfg.rmiiAddr, cfg.rmiiMdc, cfg.rmiiMdio, cfg.rmiiPwr, cfg.rmiiClk);
    s_ethRmiiUpDone = false;
    xTaskCreatePinnedToCore(ethRmiiUpTask, "ethrmii", 8192, nullptr, 5, nullptr, 0);
    uint32_t t0 = millis();
    while (!s_ethRmiiUpDone && millis() - t0 < 30000) delay(20);
    if (!s_ethRmiiUpDone) Serial.println("[ETH] RMII core-0 bring-up still running after 30s");
}
#endif

void startWiredEth() {
#if defined(HAS_ETH_SPI) && defined(HAS_ETH_RMII)
    if (cfg.wiredPhy == WIRED_PHY_RMII) startEthRmii();
    else                                startEthSpi();
#elif defined(HAS_ETH_SPI)
    startEthSpi();
#elif defined(HAS_ETH_RMII)
    startEthRmii();
#endif
}

void applyWiredLinkLoss(bool atBoot) {
    switch (cfg.linkLossMode) {
        case WIRED_FB_AP:
            if (startWiFiAP(true)) { g_apWiredFallback = true; g_ethFallback = true; }
            break;
        case WIRED_FB_WIFI:
            if (!cfg.wifiSsid.length()) break;
            if (atBoot) {
                Serial.println("[NET] no wired link at boot -> joining WiFi");
                g_useEth = false; g_ethFallback = true;
                startWiFiStation();
            } else {
                Serial.println("[NET] wired link lost -> reboot");
                delay(200); ESP.restart();
            }
            break;
        case WIRED_FB_REBOOT:
            if (!atBoot) { Serial.println("[NET] wired link lost -> reboot"); delay(200); ESP.restart(); }
            break;
        case WIRED_FB_RETRY:
        default:
            break;
    }
}
