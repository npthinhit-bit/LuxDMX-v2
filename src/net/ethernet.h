#pragma once
#include "config_enums.h"
#include <ETH.h>

// Ethernet bring-up for wired boards. Runtime-selected between SPI (W5500/DM9051)
// and RMII (ESP32 internal EMAC). Both paths bring the link up from a core-0 task
// so the EMAC/SPI interrupts stay off core 1 (where DMX/RDM TX runs).
void startWiredEth();
bool startWiFiAP(bool requirePw);

// Default SPI-Ethernet pin wiring (matches classic ESP32 / most W5500 modules).
// A board template overrides via build_flags; HAS_ETH_SPI builds include the W5500
// driver but never call ETH.begin() with these defaults on an RMII-only build.
#ifndef ETH_W5500_SCK
#define ETH_W5500_SCK 18
#endif
#ifndef ETH_W5500_MOSI
#define ETH_W5500_MOSI 23
#endif
#ifndef ETH_W5500_MISO
#define ETH_W5500_MISO 19
#endif
#ifndef ETH_W5500_CS
#define ETH_W5500_CS 5
#endif
#ifndef ETH_W5500_IRQ
#define ETH_W5500_IRQ 4
#endif
#ifndef ETH_W5500_RST
#define ETH_W5500_RST 25
#endif
#ifndef ETH_W5500_SPI_FREQ_MHZ
#define ETH_W5500_SPI_FREQ_MHZ 20
#endif
#ifdef HAS_ETH_SPI
#ifndef ETH_W5500_SPI_HOST
#define ETH_W5500_SPI_HOST SPI3_HOST
#endif
#ifndef ETH_W5500_ADDR
#define ETH_W5500_ADDR 1
#endif
#endif

// Wired PHY family selection (cfg.wiredPhy).
#define WIRED_PHY_SPI 0
#define WIRED_PHY_RMII 1
#define ETH_SPI_PHY_W5500 0
#define ETH_SPI_PHY_DM9051 1

// Link-loss fallback policy (cfg.linkLossMode) when wired Ethernet has no link.

// Apply the configured link-loss policy. atBoot=true: never reboot (would loop).
void applyWiredLinkLoss(bool atBoot);

// Static IP for the wired interface (mirrors WiFi's applyStaStaticIp).
void applyEthStaticIp();

// Block until the link is up (or timeout). Called from the core-0 bring-up task.
void waitEthLink();
